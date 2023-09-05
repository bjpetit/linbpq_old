

static char NavBarStyleSheet[] =
  "body { margin: 0; font-family: Arial, Helvetica, sans-serif; }"
  ".navbar { overflow: hidden; background-color: white; }"
  ".navbar a { float: left; display: block; color: black; text-align: center; "
  "padding: 10px 12px; text-decoration: none; font-size: 14px; }"
  ".navbar a:hover, .subnav:hover .subnavbtn { background-color: #ddd; color: black; }"
  ".navbar a.active { background-color: #04AA6D; color: white; }"
  ".navbar .icon { display: none; }"
  ".subnav { overflow: hidden; background-color: white; }"
  ".subnav .subnavbtn { font-size: inherit; border: none; outline: none; color: inherit; "
  "padding: 10px 12px; background-color: inherit; font-family: inherit; margin: 0; }"
  ".subnav-content { display: none; position: absolute; left: 0; background-color: inherit; width: 100%; z-index: 1; }"
  ".subnav-content a { float: left; color: inherit; text-decoration: none;}"
  ".subnav-content a:hover { background-color: #eee; color: black; }"
  ".subnav:hover .subnav-content { display: block; }"
  "@media screen and (max-width: 600px) { .navbar a:not(:first-child) {display: none;}"
  ".navbar a.icon { float: right; display: block; } }"
  "@media screen and (max-width: 600px) { .navbar.responsive {position: relative;}"
  ".navbar.responsive .icon { position: absolute; right: 0; top: 0; }"
  ".navbar.responsive a { float: none; display: block; text-align: left; }"
  ".subnav.responsive a { float: none; display: block; text-align: left;  }"
  ".subnav-content.responsive a { float: none; display: block; text-align: left;  }}"
;

static char NavBarElement[] =
  "<a href=\"javascript:void(0);\" class=\"icon\" onclick=\"navBarSelect()\">"
  "<i class=\"fa fa-bars\"></i></a>"
;

static char NavBarScript[] =
  "function navBarSelect() {"
  "var x = document.getElementById(\"navBar\");"
  "if (x.className === \"navbar\" || x.className === \"subnav-content\") {"
  "  x.className += \" responsive\";"
  "} else {"
  "  x.className = \"navbar\";"
  "} }\r\n"
;