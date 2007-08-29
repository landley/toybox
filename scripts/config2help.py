#!/usr/bin/python

import os,sys

def zapquotes(str):
  if str[0]=='"': str = str[1:str.rfind('"')]
  return str

def escapequotes(str):
  return str.strip().replace("\\","\\\\").replace('"','\\"')

helplen = morelines = 0
out = sys.stdout

def readfile(filename):
  global helplen, morelines
  #sys.stderr.write("Reading %s\n" % filename)
  try:
    lines = open(filename).read().split("\n")
  except IOError:
    sys.stderr.write("File %s missing\n" % filename)
    return
  config = None
  description = None
  for i in lines:
    if helplen:
      i = i.expandtabs()
      if not len(i) or i[:helplen].isspace():
        if morelines: out.write('\\n')
        morelines = 1
        out.write(escapequotes(i))
        continue
      else:
        helplen = morelines = 0
        out.write('"\n')

    words = i.strip().split(None,1)
    if not len(words): continue

    if words[0] in ("config", "menuconfig"):
      config = words[1]
      description = ""
    elif words[0] in ("bool", "boolean", "tristate", "string", "hex", "int"):
       if len(words)>1: description = zapquotes(words[1])
    elif words[0]=="prompt":
      description = htmlescape(zapquotes(words[1]))
    elif words[0] in ("help", "---help---"):
      out.write('#define help_%s "' % config.lower())
      helplen = len(i[:i.find(words[0])].expandtabs())
    elif words[0] == "source": readfile(zapquotes(words[1]))
    elif words[0] in ("default","depends", "select", "if", "endif", "#", "comment", "menu", "endmenu"): pass

readfile(sys.argv[1])
if helplen: out.write('"\n')
