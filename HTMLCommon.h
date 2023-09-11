

static char NavBarStyleSheet[] =
  ":root{\n"
    "--white: #f9f9f9;\n"
    "--black: #36383F;\n"
    "--gray: #85888C;\n"
  "}\n"
   /* variables*/
  /* Reset */
  "*{\n"
    "margin: 0;\n"
    "padding: 0;\n"
    "box-sizing: border-box;\n"
  "}\n"
  "body{\n"
    "background-color: var(--white);\n"
    "font-family: \"Poppins\", sans-serif;\n"
  "}\n"
  "a{\n"
    "text-decoration: none;\n"
  "}\n"
  "ul{\n"
    "list-style: none;\n"
  "}\n"
  /* Header */
  ".header {\n"
    "background-color: var(--black);\n"
    "box-shadow: 1px 1px 5px 0px var(--gray);\n"
    "position: sticky;\n"
    "top: 0;\n"
    "width: 100%;\n"
  "}\n"
  /* Title */
  ".logo{\n"
    "display: inline-block;\n"
    "color: var(--white);\n"
    "font-size: 60px;\n"
    "margin-left: 10px;\n"
  "}"
  /* Nav menu */
  ".nav{"
    "width: 100%;\n"
    "height: 100%;\n"
    "position: fixed;\n"
    "top: 80px;\n"
    "background-color: var(--black);\n"
    "overflow: hidden;\n"
  "}\n"
  ".menu a{\n"
    "display: block;\n"
    "padding: 30px;\n"
    "color: var(--white);\n"
  "}\n"
  ".menu a:hover{\n"
    "background-color: var(--gray);\n"
  "}\n"
  ".nav{\n"
    "max-height: 0;\n"
    "transition: max-height .5s ease-out;\n"
  "}\n"
  ".hamb{\n"
    "cursor: pointer;\n"
    "float: right;\n"
    "padding: 40px 20px;\n"
  "}\n"
  /* Style label tag */

  ".hamb-line {\n"
    "background: var(--white);\n"
    "display: block;\n"
    "height: 2px;\n"
    "position: relative;\n"
    "width: 24px;\n"
  "}\n" 
  /* Style span tag */

  ".hamb-line::before,\n"
  ".hamb-line::after{\n"
    "background: var(--white);\n"
    "content: '';\n"
    "display: block;\n"
    "height: 100%;\n"
    "position: absolute;\n"
    "transition: all .2s ease-out;\n"
    "width: 100%;\n"
  "}\n"
  ".hamb-line::before{\n"
    "top: 5px;\n"
  "}\n"
  ".hamb-line::after{\n"
    "top: -5px;"
  "}\n"

  ".side-menu {\n"
    "display: none;\n"
  "}\n"
  /* Hide checkbox */
  ".side-menu:checked ~ nav{\n"
    "max-height: 100%;\n"
  "}\n"
  ".side-menu:checked ~ .hamb .hamb-line {\n"
    "background: transparent;\n"
  "}\n"
  ".side-menu:checked ~ .hamb .hamb-line::before {\n"
    "transform: rotate(-45deg);\n"
    "top:0;\n"
  "}\n"
  ".side-menu:checked ~ .hamb .hamb-line::after {\n"
    "transform: rotate(45deg);\n"
    "top:0;\n"
  "}\n"
  /* Responsiveness */
  "@media (min-width: 768px) {\n"
    ".nav{\n"
        "max-height: none;\n"
        "top: 0;\n"
        "position: relative;\n"
        "float: right;\n"
        "width: fit-content;\n"
        "background-color: transparent;\n"
    "}\n"
    ".menu li{\n"
        "float: left;\n"
    "}\n"
    ".menu a:hover{\n"
        "background-color: transparent;\n"
        "color: var(--gray);\n"
    "}"\n

    ".hamb{\n"
        "display: none;\n"
    "}\n"
  "}\n"
;