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

stuff,blah=readit(["sed","-n", 's/<span id=\\([a-z_]*\\)>/\\1 /;t good;d;:good;h;:loop;n;s@</span>@@;t out;H;b loop;:out;g;s/\\n/ /g;p', "www/roadmap.html", "www/status.html"])

blah,toystuff=readit(["./toybox"])

reverse={}
for i in stuff:
  for j in stuff[i]:
    try: reverse[j].append(i)
    except: reverse[j]=[i]

for i in toystuff:
  try:
    if ("ready" in reverse[i]) or ("pending" in reverse[i]): continue
  except: pass
  print i

pending=[]
done=[]

print "all commands=%s" % len(reverse)

outfile=open("www/status.gen", "w")
outfile.write("<a name=all><h2><a href=#all>All commands</a></h2><blockquote><p>\n")

blah=list(reverse)
blah.sort()
for i in blah:
  out=i
  if "posix" in reverse[i]: out='[<a href="http://pubs.opengroup.org/onlinepubs/9699919799/utilities/%s.html">%s</a>]' % (i,out)
  elif "lsb" in reverse[i]: out='&lt;<a href="http://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/%s.html">%s</a>&gt;' % (i,out)
  elif "development" in reverse[i]: out='(<a href="http://linux.die.net/man/1/%s">%s</a>)' % (i,out)
  elif "toolbox" in reverse[i]: out='{%s}' % out
  elif "klibc_cmd" in reverse[i]: out='=%s=' % out
  elif "sash_cmd" in reverse[i]: out='#%s#' % out
  elif "sbase_cmd" in reverse[i]: out='@%s@' % out
  elif "beastiebox_cmd" in reverse[i]: out='*%s*' % out
  elif "request" in reverse[i]: out='+<a href="http://linux.die.net/man/1/%s">%s</a>+' % (i,out)
  elif "ready" in reverse[i]: pass
  else: sys.stderr.write("unknown %s %s\n" % (i, reverse[i]))
  if "ready" in reverse[i] or "pending" in reverse[i]:
    done.append(out)
    out='<strike>%s</strike>' % out
  else: pending.append(out)

  outfile.write(out+"\n")

print "done=%s" % len(done)
outfile.write("</p></blockquote>\n")

outfile.write("<a name=todo><h2><a href=#todo>TODO</a></h2><blockquote><p>%s</p></blockquote>\n" % "\n".join(pending))
outfile.write("<a name=done><h2><a href=#done>Done</a></h2><blockquote><p>%s</p></blockquote>\n" % "\n".join(done))
