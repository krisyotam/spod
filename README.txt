spod


Simple Podcast. A static podcast page generator in the style of stagit.

Point it at a directory of podcast folders. It generates plain HTML you
can serve with any web server. No JavaScript, no database, no login, no
dependencies beyond a C compiler.


HOW IT WORKS

  Each subdirectory under your podcast root is treated as one podcast.
  Inside each podcast directory:

  - Audio and video files (.mp3, .ogg, .opus, .m4a, .flac, .wav, .mp4,
    .mkv, .webm)
  - README or README.md or README.txt (optional, displayed on the readme
    page; first line is the heading, second line becomes the description
    on the index)
  - COVER.jpg or cover.png or similar (optional, displayed on the log
    page)
  - LICENSE or COPYING (optional, displayed on the license page)


  EXAMPLE LAYOUT

    podcasts/
      my-show/
        COVER.jpg
        README.md
        LICENSE
        001-first-episode.mp3
        002-second-episode.mp3
      another-show/
        cover.png
        README
        intro.ogg
        bonus.mp4


  GENERATED OUTPUT

    For each podcast, spod generates four pages (just like stagit
    generates log, files, refs):

    - log.html      episode list sorted newest-first with date and size
                    (the default landing page)
    - episodes.html file listing with permissions, filenames, and sizes
    - readme.html   the README rendered as preformatted text
    - license.html  the LICENSE rendered as preformatted text

    The index page lists all podcasts with their name, description,
    owner, and last update date. Navigation between pages uses the same
    Log | Episodes | README | LICENSE bar that stagit uses for
    Log | Files | Refs.

    Media files are symlinked into the output directory, not copied.


USAGE

  spod podcasts/              output to out/
  spod podcasts/ /var/www     output to /var/www

  Serve the output with nginx, quark, or python3 -m http.server.


BUILD

  make
  sudo make install


CONFIGURATION

  Copy config.def.h to config.h and edit before building:

    static char sitetitle[] = "podcasts";
    static char sitedesc[]  = "my podcast collection";
    static char siteowner[] = "kris";
    static char favicon[]   = "";


DESIGN

  - C99, POSIX.1-2008. No external libraries.
  - Single file, around 600 lines.
  - Produces a ~22KB static binary.
  - Dark mode support via prefers-color-scheme.
  - Same CSS and HTML structure as stagit. If you know what stagit looks
    like, you know what spod looks like.


LICENSE

  MIT
