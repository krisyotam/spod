/* spod - static podcast page generator */
/* like stagit, but for podcasts */

#define _XOPEN_SOURCE 700

#include <sys/stat.h>
#include <sys/types.h>

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include "config.h"

#define LEN(s) (sizeof(s)/sizeof(*(s)))

struct episode {
	char   name[PATH_MAX];    /* filename without extension */
	char   path[PATH_MAX];    /* relative path to file */
	off_t  size;              /* file size in bytes */
	time_t mtime;             /* modification time */
};

struct podcast {
	char           name[PATH_MAX];
	char           dir[PATH_MAX];
	char           desc[4096];       /* description (2nd line+ of README) */
	char           readme[65536];
	char           license[65536];
	char           cover[PATH_MAX];  /* relative path to cover image */
	char           coverext[16];     /* image extension */
	int            hasreadme;
	int            haslicense;
	struct episode *eps;
	size_t         neps;
	size_t         capsz;
};

static char outdir[PATH_MAX];

static void
die(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	exit(1);
}

static void
checkfileerror(FILE *fp, const char *name, int mode)
{
	if (mode == 'r' && ferror(fp))
		die("read error: %s", name);
	else if (mode == 'w' && (fflush(fp) || ferror(fp)))
		die("write error: %s", name);
}

static void
xmlencode(FILE *fp, const char *s)
{
	for (; *s; s++) {
		switch (*s) {
		case '<':  fputs("&lt;",   fp); break;
		case '>':  fputs("&gt;",   fp); break;
		case '\'': fputs("&#39;",  fp); break;
		case '&':  fputs("&amp;",  fp); break;
		case '"':  fputs("&quot;", fp); break;
		default:   fputc(*s, fp);
		}
	}
}

static const char *
humansize(off_t bytes)
{
	static char buf[32];
	double s = bytes;

	if (s < 1024.0)
		snprintf(buf, sizeof(buf), "%.0fB", s);
	else if (s < 1048576.0)
		snprintf(buf, sizeof(buf), "%.1fKB", s / 1024.0);
	else if (s < 1073741824.0)
		snprintf(buf, sizeof(buf), "%.1fMB", s / 1048576.0);
	else
		snprintf(buf, sizeof(buf), "%.1fGB", s / 1073741824.0);

	return buf;
}

static void
formattime(char *buf, size_t bufsz, time_t t)
{
	struct tm *tm;

	tm = localtime(&t);
	if (tm)
		strftime(buf, bufsz, "%Y-%m-%d %H:%M", tm);
	else
		buf[0] = '\0';
}

static void
formattimeshort(char *buf, size_t bufsz, time_t t)
{
	struct tm *tm;

	tm = localtime(&t);
	if (tm)
		strftime(buf, bufsz, "%Y-%m-%d", tm);
	else
		buf[0] = '\0';
}

static int
mkdirp(const char *path)
{
	char tmp[PATH_MAX];
	char *p;
	size_t len;

	snprintf(tmp, sizeof(tmp), "%s", path);
	len = strlen(tmp);
	if (len > 0 && tmp[len - 1] == '/')
		tmp[len - 1] = '\0';

	for (p = tmp + 1; *p; p++) {
		if (*p == '/') {
			*p = '\0';
			if (mkdir(tmp, 0755) < 0 && errno != EEXIST)
				return -1;
			*p = '/';
		}
	}
	if (mkdir(tmp, 0755) < 0 && errno != EEXIST)
		return -1;

	return 0;
}

static int
isimgext(const char *ext)
{
	return (!strcmp(ext, ".jpg") || !strcmp(ext, ".jpeg") ||
	        !strcmp(ext, ".png") || !strcmp(ext, ".gif") ||
	        !strcmp(ext, ".webp") || !strcmp(ext, ".svg") ||
	        !strcmp(ext, ".bmp") || !strcmp(ext, ".ico"));
}

static int
ismediaext(const char *ext)
{
	return (!strcmp(ext, ".mp3") || !strcmp(ext, ".ogg") ||
	        !strcmp(ext, ".opus") || !strcmp(ext, ".m4a") ||
	        !strcmp(ext, ".flac") || !strcmp(ext, ".wav") ||
	        !strcmp(ext, ".mp4") || !strcmp(ext, ".mkv") ||
	        !strcmp(ext, ".webm"));
}

static int
epcmp(const void *a, const void *b)
{
	const struct episode *ea = a, *eb = b;

	/* newest first */
	if (ea->mtime > eb->mtime)
		return -1;
	if (ea->mtime < eb->mtime)
		return 1;
	return strcmp(ea->name, eb->name);
}

static void
ep_add(struct podcast *p, const char *fname, const char *relpath,
       off_t size, time_t mtime)
{
	char *dot;

	if (p->neps >= p->capsz) {
		p->capsz = p->capsz ? p->capsz * 2 : 16;
		p->eps = realloc(p->eps, p->capsz * sizeof(*p->eps));
		if (!p->eps)
			die("realloc");
	}
	snprintf(p->eps[p->neps].path, sizeof(p->eps[p->neps].path),
	         "%s", relpath);
	snprintf(p->eps[p->neps].name, sizeof(p->eps[p->neps].name),
	         "%s", fname);
	dot = strrchr(p->eps[p->neps].name, '.');
	if (dot)
		*dot = '\0';
	p->eps[p->neps].size = size;
	p->eps[p->neps].mtime = mtime;
	p->neps++;
}

static void
readfile(const char *path, char *buf, size_t bufsz)
{
	FILE *fp;
	size_t n;

	fp = fopen(path, "r");
	if (!fp) {
		buf[0] = '\0';
		return;
	}
	n = fread(buf, 1, bufsz - 1, fp);
	buf[n] = '\0';
	fclose(fp);
}

/* extract description: skip first line (heading), take rest */
static void
extractdesc(const char *readme, char *desc, size_t descsz)
{
	const char *p;

	desc[0] = '\0';
	if (!readme[0])
		return;

	/* skip first line */
	p = strchr(readme, '\n');
	if (!p || !*(p + 1))
		return;
	p++;

	/* skip blank lines after heading */
	while (*p == '\n' || *p == '\r')
		p++;

	if (!*p)
		return;

	snprintf(desc, descsz, "%s", p);

	/* truncate at first newline for index use */
	{
		char *nl = strchr(desc, '\n');
		if (nl)
			*nl = '\0';
	}
}

static void
podcast_scan(struct podcast *p, const char *dir)
{
	DIR *dp;
	struct dirent *de;
	struct stat st;
	char path[PATH_MAX], *ext;

	snprintf(p->dir, sizeof(p->dir), "%s", dir);

	/* extract podcast name from directory basename */
	{
		const char *base = strrchr(dir, '/');
		base = base ? base + 1 : dir;
		snprintf(p->name, sizeof(p->name), "%s", base);
	}

	dp = opendir(dir);
	if (!dp)
		die("opendir: %s: %s", dir, strerror(errno));

	p->readme[0] = '\0';
	p->license[0] = '\0';
	p->desc[0] = '\0';
	p->cover[0] = '\0';
	p->coverext[0] = '\0';
	p->hasreadme = 0;
	p->haslicense = 0;
	p->eps = NULL;
	p->neps = 0;
	p->capsz = 0;

	while ((de = readdir(dp))) {
		if (de->d_name[0] == '.')
			continue;

		snprintf(path, sizeof(path), "%s/%s", dir, de->d_name);
		if (stat(path, &st) < 0)
			continue;
		if (!S_ISREG(st.st_mode))
			continue;

		/* README */
		if (!strcmp(de->d_name, "README") ||
		    !strcmp(de->d_name, "README.md") ||
		    !strcmp(de->d_name, "README.txt")) {
			readfile(path, p->readme, sizeof(p->readme));
			p->hasreadme = 1;
			extractdesc(p->readme, p->desc, sizeof(p->desc));
			continue;
		}

		/* LICENSE */
		if (!strcmp(de->d_name, "LICENSE") ||
		    !strcmp(de->d_name, "LICENSE.md") ||
		    !strcmp(de->d_name, "LICENSE.txt") ||
		    !strcmp(de->d_name, "COPYING")) {
			readfile(path, p->license, sizeof(p->license));
			p->haslicense = 1;
			continue;
		}

		ext = strrchr(de->d_name, '.');
		if (!ext)
			continue;

		/* cover image */
		if (isimgext(ext) &&
		    (!strncasecmp(de->d_name, "COVER", 5) ||
		     !strncasecmp(de->d_name, "cover", 5) ||
		     !strncasecmp(de->d_name, "artwork", 7) ||
		     !strncasecmp(de->d_name, "poster", 6) ||
		     !strncasecmp(de->d_name, "logo", 4))) {
			snprintf(p->cover, sizeof(p->cover), "%s", de->d_name);
			snprintf(p->coverext, sizeof(p->coverext), "%s", ext);
			continue;
		}

		/* media files (audio + video) */
		if (ismediaext(ext))
			ep_add(p, de->d_name, de->d_name, st.st_size,
			       st.st_mtime);
	}

	closedir(dp);
	qsort(p->eps, p->neps, sizeof(*p->eps), epcmp);
}

/* write header exactly like stagit */
static void
writeheader(FILE *fp, const struct podcast *p, const char *page)
{
	fputs("<!DOCTYPE html>\n<html>\n<head>\n"
	      "<meta http-equiv=\"Content-Type\""
	      " content=\"text/html; charset=UTF-8\" />\n"
	      "<meta name=\"viewport\""
	      " content=\"width=device-width, initial-scale=1\" />\n"
	      "<title>", fp);
	xmlencode(fp, p->name);
	if (p->desc[0]) {
		fputs(" - ", fp);
		xmlencode(fp, p->desc);
	}
	fputs("</title>\n"
	      "<link rel=\"stylesheet\" type=\"text/css\""
	      " href=\"style.css\" />\n"
	      "</head>\n<body>\n", fp);

	/* top bar: logo + name + desc */
	fputs("<table><tr><td>", fp);
	if (favicon[0]) {
		fputs("<a href=\"../index.html\">"
		      "<img src=\"", fp);
		xmlencode(fp, favicon);
		fputs("\" alt=\"\" width=\"32\" height=\"32\" />"
		      "</a>", fp);
	}
	fputs("</td><td><h1>", fp);
	xmlencode(fp, p->name);
	fputs("</h1><span class=\"desc\">", fp);
	xmlencode(fp, p->desc);
	fputs("</span></td></tr><tr><td></td><td>\n", fp);

	/* nav links: Log | Episodes | README | LICENSE */
	fputs("<a href=\"log.html\">Log</a> | \n", fp);
	fputs("<a href=\"episodes.html\">Episodes</a>", fp);
	if (p->hasreadme)
		fputs(" | \n<a href=\"readme.html\">README</a>", fp);
	if (p->haslicense)
		fputs(" | \n<a href=\"license.html\">LICENSE</a>", fp);

	fputs("\n</td></tr></table>\n<hr/>\n<div id=\"content\">\n", fp);

	(void)page;
}

static void
writefooter(FILE *fp)
{
	fputs("</div>\n</body>\n</html>\n", fp);
}

/* index page: like stagit-index */
static void
writeindexheader(FILE *fp)
{
	fputs("<!DOCTYPE html>\n<html>\n<head>\n"
	      "<meta http-equiv=\"Content-Type\""
	      " content=\"text/html; charset=UTF-8\" />\n"
	      "<meta name=\"viewport\""
	      " content=\"width=device-width, initial-scale=1\" />\n"
	      "<title>", fp);
	xmlencode(fp, sitetitle);
	fputs("</title>\n"
	      "<link rel=\"stylesheet\" type=\"text/css\""
	      " href=\"style.css\" />\n"
	      "</head>\n<body>\n", fp);

	fputs("<table><tr><td>", fp);
	if (favicon[0]) {
		fputs("<img src=\"", fp);
		xmlencode(fp, favicon);
		fputs("\" alt=\"\" width=\"32\" height=\"32\" />", fp);
	}
	fputs("</td><td><span class=\"desc\">", fp);
	xmlencode(fp, sitetitle);
	if (sitedesc[0]) {
		fputs(" - ", fp);
		xmlencode(fp, sitedesc);
	}
	fputs("</span></td></tr>\n"
	      "<tr><td></td><td></td></tr>\n"
	      "</table>\n<hr/>\n<div id=\"content\">\n", fp);
}

static void
writeindex(struct podcast *pods, size_t npods)
{
	FILE *fp;
	char path[PATH_MAX], timebuf[64];
	size_t i;
	time_t latest;

	snprintf(path, sizeof(path), "%s/index.html", outdir);
	fp = fopen(path, "w");
	if (!fp)
		die("fopen: %s: %s", path, strerror(errno));

	writeindexheader(fp);

	fputs("<table id=\"index\"><thead>\n<tr>"
	      "<td><b>Name</b></td>"
	      "<td><b>Description</b></td>"
	      "<td><b>Owner</b></td>"
	      "<td><b>Last commit</b></td>"
	      "</tr>\n</thead><tbody>\n", fp);

	for (i = 0; i < npods; i++) {
		latest = 0;
		if (pods[i].neps > 0)
			latest = pods[i].eps[0].mtime;

		formattimeshort(timebuf, sizeof(timebuf), latest);

		fputs("<tr><td><a href=\"", fp);
		xmlencode(fp, pods[i].name);
		fputs("/log.html\">", fp);
		xmlencode(fp, pods[i].name);
		fputs("</a></td><td>", fp);
		xmlencode(fp, pods[i].desc);
		fputs("</td><td>", fp);
		if (siteowner[0])
			xmlencode(fp, siteowner);
		fputs("</td><td>", fp);
		if (latest)
			xmlencode(fp, timebuf);
		fputs("</td></tr>\n", fp);
	}

	fputs("</tbody>\n</table>\n", fp);
	writefooter(fp);
	checkfileerror(fp, path, 'w');
	fclose(fp);
}

static void
copyfile(const char *src, const char *dst)
{
	FILE *in, *out;
	char buf[8192];
	size_t n;

	in = fopen(src, "rb");
	if (!in)
		return;

	out = fopen(dst, "wb");
	if (!out) {
		fclose(in);
		return;
	}

	while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
		fwrite(buf, 1, n, out);

	fclose(in);
	fclose(out);
}

/* log.html — like stagit's log page */
static void
writelog(const struct podcast *p, const char *poddir)
{
	FILE *fp;
	char path[PATH_MAX], timebuf[64];
	size_t i;

	snprintf(path, sizeof(path), "%s/log.html", poddir);
	fp = fopen(path, "w");
	if (!fp)
		die("fopen: %s: %s", path, strerror(errno));

	writeheader(fp, p, "log");

	/* cover image at top */
	if (p->cover[0]) {
		fputs("<div id=\"cover\">\n<img src=\"", fp);
		xmlencode(fp, p->cover);
		fputs("\" alt=\"cover\" />\n</div>\n", fp);
	}

	fputs("<table id=\"log\"><thead>\n<tr>"
	      "<td><b>Date</b></td>"
	      "<td><b>Episode</b></td>"
	      "<td class=\"num\" align=\"right\"><b>Size</b></td>"
	      "</tr>\n</thead><tbody>\n", fp);

	for (i = 0; i < p->neps; i++) {
		formattime(timebuf, sizeof(timebuf), p->eps[i].mtime);
		fputs("<tr><td>", fp);
		xmlencode(fp, timebuf);
		fputs("</td><td><a href=\"", fp);
		xmlencode(fp, p->eps[i].path);
		fputs("\">", fp);
		xmlencode(fp, p->eps[i].name);
		fputs("</a></td><td class=\"num\" align=\"right\">", fp);
		fputs(humansize(p->eps[i].size), fp);
		fputs("</td></tr>\n", fp);
	}

	fputs("</tbody>\n</table>\n", fp);
	writefooter(fp);
	checkfileerror(fp, path, 'w');
	fclose(fp);
}

/* episodes.html — like stagit's files page */
static void
writeepisodes(const struct podcast *p, const char *poddir)
{
	FILE *fp;
	char path[PATH_MAX], timebuf[64];
	size_t i;

	snprintf(path, sizeof(path), "%s/episodes.html", poddir);
	fp = fopen(path, "w");
	if (!fp)
		die("fopen: %s: %s", path, strerror(errno));

	writeheader(fp, p, "episodes");

	fputs("<table id=\"files\"><thead>\n<tr>"
	      "<td><b>Mode</b></td>"
	      "<td><b>Name</b></td>"
	      "<td class=\"num\" align=\"right\"><b>Size</b></td>"
	      "</tr>\n</thead><tbody>\n", fp);

	for (i = 0; i < p->neps; i++) {
		formattime(timebuf, sizeof(timebuf), p->eps[i].mtime);
		fputs("<tr><td>-rw-r--r--</td><td><a href=\"", fp);
		xmlencode(fp, p->eps[i].path);
		fputs("\">", fp);
		xmlencode(fp, p->eps[i].path);
		fputs("</a></td><td class=\"num\" align=\"right\">", fp);
		fputs(humansize(p->eps[i].size), fp);
		fputs("</td></tr>\n", fp);
	}

	/* also list README and LICENSE as files */
	if (p->hasreadme) {
		fputs("<tr><td>-rw-r--r--</td>"
		      "<td><a href=\"readme.html\">README</a></td>"
		      "<td class=\"num\" align=\"right\"></td></tr>\n", fp);
	}
	if (p->haslicense) {
		fputs("<tr><td>-rw-r--r--</td>"
		      "<td><a href=\"license.html\">LICENSE</a></td>"
		      "<td class=\"num\" align=\"right\"></td></tr>\n", fp);
	}
	if (p->cover[0]) {
		fputs("<tr><td>-rw-r--r--</td><td>", fp);
		xmlencode(fp, p->cover);
		fputs("</td><td class=\"num\" align=\"right\"></td>"
		      "</tr>\n", fp);
	}

	fputs("</tbody>\n</table>\n", fp);
	writefooter(fp);
	checkfileerror(fp, path, 'w');
	fclose(fp);
}

/* readme.html */
static void
writereadme(const struct podcast *p, const char *poddir)
{
	FILE *fp;
	char path[PATH_MAX];

	if (!p->hasreadme)
		return;

	snprintf(path, sizeof(path), "%s/readme.html", poddir);
	fp = fopen(path, "w");
	if (!fp)
		die("fopen: %s: %s", path, strerror(errno));

	writeheader(fp, p, "readme");
	fputs("<pre id=\"readme\">", fp);
	xmlencode(fp, p->readme);
	fputs("</pre>\n", fp);
	writefooter(fp);
	checkfileerror(fp, path, 'w');
	fclose(fp);
}

/* license.html */
static void
writelicense(const struct podcast *p, const char *poddir)
{
	FILE *fp;
	char path[PATH_MAX];

	if (!p->haslicense)
		return;

	snprintf(path, sizeof(path), "%s/license.html", poddir);
	fp = fopen(path, "w");
	if (!fp)
		die("fopen: %s: %s", path, strerror(errno));

	writeheader(fp, p, "license");
	fputs("<pre id=\"license\">", fp);
	xmlencode(fp, p->license);
	fputs("</pre>\n", fp);
	writefooter(fp);
	checkfileerror(fp, path, 'w');
	fclose(fp);
}

static void
writepodcast(const struct podcast *p)
{
	char poddir[PATH_MAX];
	size_t i;

	snprintf(poddir, sizeof(poddir), "%s/%s", outdir, p->name);
	if (mkdirp(poddir) < 0)
		die("mkdirp: %s: %s", poddir, strerror(errno));

	/* symlink media files instead of copying */
	for (i = 0; i < p->neps; i++) {
		char src[PATH_MAX], dst[PATH_MAX], abssrc[PATH_MAX];

		snprintf(src, sizeof(src), "%s/%s",
		         p->dir, p->eps[i].path);
		snprintf(dst, sizeof(dst), "%s/%s",
		         poddir, p->eps[i].path);

		if (!realpath(src, abssrc))
			die("realpath: %s: %s", src, strerror(errno));

		unlink(dst);
		if (symlink(abssrc, dst) < 0)
			die("symlink: %s -> %s: %s", dst, abssrc,
			    strerror(errno));
	}

	/* copy cover image */
	if (p->cover[0]) {
		char src[PATH_MAX], dst[PATH_MAX];
		snprintf(src, sizeof(src), "%s/%s", p->dir, p->cover);
		snprintf(dst, sizeof(dst), "%s/%s", poddir, p->cover);
		copyfile(src, dst);
	}

	/* generate pages */
	writelog(p, poddir);
	writeepisodes(p, poddir);
	writereadme(p, poddir);
	writelicense(p, poddir);
}

static void
writestyle(void)
{
	FILE *fp;
	char path[PATH_MAX];

	snprintf(path, sizeof(path), "%s/style.css", outdir);
	fp = fopen(path, "w");
	if (!fp)
		die("fopen: %s: %s", path, strerror(errno));

	fputs(
	"body {\n"
	"\tcolor: #000;\n"
	"\tbackground-color: #fff;\n"
	"\tfont-family: monospace;\n"
	"}\n"
	"h1, h2, h3, h4, h5, h6 {\n"
	"\tfont-size: 1em;\n"
	"\tmargin: 0;\n"
	"}\n"
	"img, h1, h2 {\n"
	"\tvertical-align: middle;\n"
	"}\n"
	"img {\n"
	"\tborder: 0;\n"
	"}\n"
	"#cover img {\n"
	"\tmax-width: 200px;\n"
	"\tmax-height: 200px;\n"
	"\tmargin: 0.5em 0;\n"
	"}\n"
	"a {\n"
	"\tcolor: #00e;\n"
	"\ttext-decoration: none;\n"
	"}\n"
	"a:hover {\n"
	"\ttext-decoration: underline;\n"
	"}\n"
	"table thead td {\n"
	"\tfont-weight: bold;\n"
	"}\n"
	"table td {\n"
	"\tpadding: 0 0.4em;\n"
	"}\n"
	"#content table td {\n"
	"\tvertical-align: top;\n"
	"\twhite-space: nowrap;\n"
	"}\n"
	"#index tr:hover td,\n"
	"#log tr:hover td,\n"
	"#files tr:hover td {\n"
	"\tbackground-color: #eee;\n"
	"}\n"
	"#index tr td:nth-child(2),\n"
	"#log tr td:nth-child(2) {\n"
	"\twhite-space: normal;\n"
	"}\n"
	"td.num {\n"
	"\ttext-align: right;\n"
	"}\n"
	".desc {\n"
	"\tcolor: #555;\n"
	"}\n"
	"hr {\n"
	"\tborder: 0;\n"
	"\tborder-top: 1px solid #555;\n"
	"\theight: 1px;\n"
	"}\n"
	"pre {\n"
	"\tfont-family: monospace;\n"
	"}\n"
	"pre#readme,\n"
	"pre#license {\n"
	"\twhite-space: pre-wrap;\n"
	"\tword-wrap: break-word;\n"
	"\tline-height: 1.5;\n"
	"}\n"
	"@media (prefers-color-scheme: dark) {\n"
	"\tbody {\n"
	"\t\tbackground-color: #000;\n"
	"\t\tcolor: #bdbdbd;\n"
	"\t}\n"
	"\thr {\n"
	"\t\tborder-color: #222;\n"
	"\t}\n"
	"\ta {\n"
	"\t\tcolor: #56c8ff;\n"
	"\t}\n"
	"\t.desc {\n"
	"\t\tcolor: #aaa;\n"
	"\t}\n"
	"\t#index tr:hover td,\n"
	"\t#log tr:hover td,\n"
	"\t#files tr:hover td {\n"
	"\t\tbackground-color: #111;\n"
	"\t}\n"
	"}\n", fp);

	checkfileerror(fp, path, 'w');
	fclose(fp);
}

static int
podcmp(const void *a, const void *b)
{
	const struct podcast *pa = a, *pb = b;

	return strcmp(pa->name, pb->name);
}

int
main(int argc, char *argv[])
{
	DIR *dp;
	struct dirent *de;
	struct stat st;
	struct podcast *pods;
	size_t npods, capsz;
	char path[PATH_MAX];

	if (argc < 2 || argc > 3) {
		fprintf(stderr, "usage: %s <podcastdir> [outputdir]\n",
		        argv[0]);
		return 1;
	}

	if (argc == 3)
		snprintf(outdir, sizeof(outdir), "%s", argv[2]);
	else
		snprintf(outdir, sizeof(outdir), "out");

	if (mkdirp(outdir) < 0)
		die("mkdirp: %s: %s", outdir, strerror(errno));

	dp = opendir(argv[1]);
	if (!dp)
		die("opendir: %s: %s", argv[1], strerror(errno));

	pods = NULL;
	npods = 0;
	capsz = 0;

	while ((de = readdir(dp))) {
		if (de->d_name[0] == '.')
			continue;

		snprintf(path, sizeof(path), "%s/%s", argv[1], de->d_name);
		if (stat(path, &st) < 0)
			continue;
		if (!S_ISDIR(st.st_mode))
			continue;

		if (npods >= capsz) {
			capsz = capsz ? capsz * 2 : 8;
			pods = realloc(pods, capsz * sizeof(*pods));
			if (!pods)
				die("realloc");
		}

		podcast_scan(&pods[npods], path);
		npods++;
	}

	closedir(dp);

	if (npods == 0)
		die("no podcast directories found in %s", argv[1]);

	qsort(pods, npods, sizeof(*pods), podcmp);

	/* generate output */
	writestyle();
	writeindex(pods, npods);

	for (size_t i = 0; i < npods; i++)
		writepodcast(&pods[i]);

	/* cleanup */
	for (size_t i = 0; i < npods; i++)
		free(pods[i].eps);
	free(pods);

	return 0;
}
