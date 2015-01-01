#!/usr/bin/python

# Create status.html

import subprocess,sys

def readit(args):
  ret={}
  arr=[]
  blob=subprocess.Popen(args, stdout=subprocess.PIPE, shell=False)
  for i in blob.stdout.read().split("\n"):
    if not i: continue
    i=i.split()
    ret[i[0]]=i[1:]
    arr.extend(i)
  return ret,arr

# Run sed on roadmap and status pages to get command lists, and run toybox too
# This gives us a dictionary of types, each with a list of commands

stuff,blah=readit(["sed","-n", 's/<span id=\\([a-z_]*\\)>/\\1 /;t good;d;:good;h;:loop;n;s@</span>@@;t out;H;b loop;:out;g;s/\\n/ /g;p', "www/roadmap.html", "www/status.html"])
blah,toystuff=readit(["./toybox"])

# Create reverse mappings: command is in which

reverse={}
for i in stuff:
  for j in stuff[i]:
    try: reverse[j].append(i)
    except: reverse[j]=[i]

for i in toystuff:
  try:
    if ("ready" in reverse[i]) and ("pending" in reverse[i]): print "barf", i
  except: pass
  try:
    if ("ready" in reverse[i]) or ("pending" in reverse[i]): continue
  except: pass
  print "Not ready or pending:", i

pending=[]
done=[]

print "all commands=%s" % len(reverse)

outfile=open("www/status.gen", "w")
outfile.write("<a name=all><h2><a href=#all>All commands</a></h2><blockquote><p>\n")

conv = [("posix", '<a href="http://pubs.opengroup.org/onlinepubs/9699919799/utilities/%s.html">%%s</a>', "[%s]"),
        ("lsb", '<a href="http://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/%s.html">%%s</a>', '&lt;%s&gt;'),
        ("development", '<a href="http://linux.die.net/man/1/%s">%%s</a>', '(%s)'),
        ("toolbox", "", '{%s}'), ("klibc_cmd", "", '=%s='),
        ("sash_cmd", "", '#%s#'), ("sbase_cmd", "", '@%s@'),
        ("beastiebox_cmd", "", '*%s*'), ("tizen", "", '$%s$'),
        ("request", '<a href="http://linux.die.net/man/1/%s">%%s</a>', '+%s+')]


def categorize(reverse, i, skippy=""):
  linky = "%s"
  out = i

  if skippy: types = filter(lambda a: a != skippy, reverse[i])
  else: types = reverse[i]

  for j in conv:
    if j[0] in types:
      if j[1]: linky = j[1] % i
      out = j[2] % out
      if not skippy: break
  if (not skippy) and out == i:
    sys.stderr.write("unknown %s %s\n" % (i,reverse[i]))

  return linky % out

blah=list(reverse)
blah.sort()
for i in blah:
  out=categorize(reverse, i)
  if "ready" in reverse[i] or "pending" in reverse[i]:
    done.append(out)
    out='<strike>%s</strike>' % out
  else: pending.append(out)

  outfile.write(out+"\n")

print "done=%s" % len(done)
outfile.write("</p></blockquote>\n")

outfile.write("<a name=todo><h2><a href=#todo>TODO</a></h2><blockquote><p>%s</p></blockquote>\n" % "\n".join(pending))
outfile.write("<a name=done><h2><a href=#done>Done</a></h2><blockquote><p>%s</p></blockquote>\n" % "\n".join(done))

outfile.write("<hr><h2>Categories of remaining todo items</h2>")

for i in stuff:
  todo = []

  for j in stuff[i]:
    if "ready" in reverse[j]: continue
    elif "pending" in reverse[j]: todo.append('<strike>%s</strike>' % j)
    else: todo.append(categorize(reverse,j,i))

  if todo:
    k = i
    for j in conv:
      if j[0] == i:
        k = j[2] % i

    outfile.write("<a name=%s><h2><a href=#%s>%s<a></h2><blockquote><p>" % (i,i,k))
    outfile.write(" ".join(todo))
    outfile.write("</p></blockquote>\n")
