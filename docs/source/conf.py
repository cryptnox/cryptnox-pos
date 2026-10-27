# Configuration file for the Sphinx documentation builder.
#
# This sub-site is published at docs.cryptnox.com/cryptnox-pos/. The generated
# Doxygen API reference sits under /api/ of the same tree; both are built and
# published by .github/workflows/doxygen.yml in one pass, because two workflows
# writing the same gh-pages branch would clobber each other.

import io
import os

# Derive the navy PDF cover logo from the white HTML SVG at build time, so only
# the SVG is a committed source asset. It is needed for the LaTeX build only, and
# cairosvg wants a native cairo library that a plain Windows checkout does not
# have — so a missing one skips the cover instead of blocking the HTML build.
_static = os.path.join(os.path.dirname(__file__), '_static')

try:
    import cairosvg
    from PIL import Image

    with open(os.path.join(_static, 'cryptnox-logo.svg'), encoding='utf-8') as f:
        _svg = f.read()
    _png = cairosvg.svg2png(
        bytestring=_svg.replace('fill="white"', 'fill="#101f2e"').encode(),
        output_width=1200, output_height=226,
    )
    Image.open(io.BytesIO(_png)).save(
        os.path.join(_static, 'cryptnox-logo-dark.png'), dpi=(400, 400)
    )
    _have_cover = True
except Exception as e:  # noqa: BLE001 - any failure here only costs the PDF cover
    print('conf.py: PDF cover logo not generated (%s). '
          'HTML is unaffected; for the PDF install docs/requirements.txt '
          'and a native cairo.' % e)
    _have_cover = False

# -- Project information -----------------------------------------------------

project = 'Cryptnox POS'
copyright = '2026, Cryptnox SA'
author = 'Cryptnox'
release = '1.0.0'

# -- General configuration ---------------------------------------------------

extensions = ['sphinx_sitemap']

templates_path = ['_templates']
exclude_patterns = ['_build', 'Thumbs.db', '.DS_Store']

language = 'en'

# -- SEO meta tags -----------------------------------------------------------

html_baseurl = 'https://docs.cryptnox.com/cryptnox-pos/'
html_title = 'Cryptnox POS'
sitemap_url_scheme = "{link}"

html_meta = {
    'description': 'Cryptnox POS is open-source firmware that turns an ESP32 touchscreen '
                   'board and a PN532 NFC module into a crypto payment terminal paid by '
                   'tapping a Cryptnox Hardware Wallet card.',
    'keywords': 'Cryptnox, POS, payment terminal, ESP32, Cheap Yellow Display, PN532, NFC, '
                'USDC, USDT, Ethereum, Polygon, Tron, hardware wallet, documentation',
    'author': 'Cryptnox',
    'robots': 'index, follow, max-snippet:-1, max-video-preview:-1, max-image-preview:large',
}

# -- Options for HTML output -------------------------------------------------

html_theme = 'sphinx_rtd_theme'
html_static_path = ['_static']
html_last_updated_fmt = '%Y-%m-%d'  # visible page dates — AI answers prefer dateable sources
html_logo = '_static/cryptnox-logo.svg'
html_favicon = '_static/favicon.png'

html_css_files = [
    'custom.css',
    # The four illustrated guides are set in these; their own CSS ships inside
    # each page, scoped to .cnx-guide so it cannot reach the theme.
    'https://fonts.googleapis.com/css2?family=Plus+Jakarta+Sans:wght@500;600;700'
    '&family=Inter:wght@400;500;600&family=JetBrains+Mono:wght@400;500&display=swap',
]

html_theme_options = {
    'analytics_id': 'GT-PJ7HDFB',
    'logo_only': False,
    'prev_next_buttons_location': 'none',
    'style_external_links': False,
    'style_nav_header_background': '#101f2e',
    'collapse_navigation': True,
    'sticky_navigation': True,
    'navigation_depth': 2,
    'includehidden': True,
    'titles_only': False,
}

html_show_sourcelink = False
html_copy_source = False
html_show_sphinx = False

# -- Options for PDF (LaTeX) output ------------------------------------------
# Built by CI with pdflatex; same cover and page layout as the other Cryptnox
# manuals. Output: cryptnox-pos.pdf

latex_engine = 'pdflatex'
# Generated above; the white HTML logo would be invisible on the white cover.
latex_logo = '_static/cryptnox-logo-dark.png' if _have_cover else None
latex_documents = [
    ('index', 'cryptnox-pos.tex', 'Cryptnox POS Manual', author, 'manual'),
]
latex_elements = {
    'papersize': 'a4paper',
    'pointsize': '11pt',
    'figure_align': 'H',
    'sphinxsetup': 'pre_border-radius=0pt',  # sharp rectangle corners on code-block frames
    'extraclassoptions': 'oneside,openany',  # no blank filler pages (web PDF)
    'printindex': '',  # no general Index in the PDF
    'fncychap': '',  # no fancy chapter rules; titlesec styles chapters instead
    'preamble': r'''
% Literals (\ttfamily) in Inconsolata
\usepackage{inconsolata}
% Left-align body text (ragged right instead of justified)
\usepackage[document]{ragged2e}
% Drop the "(continues on next page)" / "(continued from previous page)" labels on code blocks
\AtBeginDocument{\renewcommand*\sphinxstylecodecontinued[1]{}\renewcommand*\sphinxstylecodecontinues[1]{}}
% Whole document in the sans font (TeX Gyre Heros)
\renewcommand{\familydefault}{\sfdefault}
% Sans-serif TOC entries
\AtBeginDocument{\addtocontents{toc}{\protect\sffamily}}
% Left-aligned chapter headings
\usepackage{titlesec}
\titleformat{\chapter}[hang]{\sffamily\bfseries\huge}{\thechapter}{1em}{}
\titlespacing*{\chapter}{0pt}{0pt}{20pt}
% Centered page header (manual title) + copyright footer
\usepackage{fancyhdr}
\def\headruleskip{4pt}\def\footruleskip{4pt}% gap between header/footer text and rule
\AtBeginDocument{%
  \fancypagestyle{normal}{%
    \fancyhf{}%
    \fancyhead[C]{\sffamily\nouppercase{Cryptnox POS Manual}}%
    \fancyfoot[L]{\sffamily\copyright{} 2026 Cryptnox SA}%
    \fancyfoot[R]{\sffamily\thepage}%
    \renewcommand{\headrulewidth}{0.4pt}%
    \renewcommand{\footrulewidth}{0.4pt}%
  }%
  \fancypagestyle{plain}{%
    \fancyhf{}%
    \fancyfoot[L]{\sffamily\copyright{} 2026 Cryptnox SA}%
    \fancyfoot[R]{\sffamily\thepage}%
    \renewcommand{\headrulewidth}{0pt}%
    \renewcommand{\footrulewidth}{0.4pt}%
  }%
  \pagestyle{normal}%
}
% Cover page: logo, title, release and date; no author line (the logo brands it)
\makeatletter
\renewcommand{\sphinxmaketitle}{%
  \let\sphinxrestorepageanchorsetting\relax
  \ifHy@pageanchor\def\sphinxrestorepageanchorsetting{\Hy@pageanchortrue}\fi
  \hypersetup{pageanchor=false}%
  \begin{titlepage}%
    \let\footnotesize\small \let\footnoterule\relax
    \begingroup
      \def\endgraf{ }\def\and{\& }%
      \pdfstringdefDisableCommands{\def\\{, }}%
      \hypersetup{pdfauthor={\@author}, pdftitle={\@title}}%
    \endgroup
    \noindent\rule{\textwidth}{1pt}\par
    \begin{flushright}%
      \vskip 1em%
      \includegraphics[width=7cm]{cryptnox-logo-dark}\par
      \vskip 2em%
      {\LARGE\py@HeaderFamily \@title \par}%
      \vskip 0.5em%
      {\large\itshape \py@release\releaseinfo \par}%
      \vfill
      {\large \@date \par}%
    \end{flushright}%
    \@thanks
  \end{titlepage}%
  \setcounter{footnote}{0}%
  \let\thanks\relax\let\maketitle\relax
  \clearpage
  \ifdefined\sphinxbackoftitlepage\sphinxbackoftitlepage\fi
  \if@openright\cleardoublepage\else\clearpage\fi
  \sphinxrestorepageanchorsetting
}
\makeatother
''',
}
