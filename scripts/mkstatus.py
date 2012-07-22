#!/usr/bin/python

import subprocess,sys

stuff={}
blob=subprocess.Popen(["sed","-n", 's/<span id=\\([a-z_]*\\)>/\\1 /;t good;d;:good;h;:loop;n;s@</span>@@;t out;H;b loop;:out;g;s/\\n/ /g;p', "www/roadmap.html", "www/status.html"], stdout=subprocess.PIPE, shell=False)
for i in blob.stdout.read().split("\n"):
  if not i: continue
  i=i.split()
  stuff[i[0]]=i[1:]

stuff['toolbox'].extend(stuff['toolbox_std'])
del stuff['toolbox_std']

reverse={}
for i in stuff:
  for j in stuff[i]:
    try:
      reverse[j].append(i)
    except:
      reverse[j]=[i]

pending=[]
done=[]

outfile=open("www/status.gen", "w")
outfile.write("<h2>All commands</h2><blockquote><p>\n")

blah=list(reverse)
blah.sort()
for i in blah:
  out=i
  if "posix" in reverse[i]: out='[<a href="http://opengroup.org/onlinepubs/9699919799/utilities/%s.html">%s</a>]' % (i,out)
  elif "lsb" in reverse[i]: out='&lt;<a href="http://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/%s.html">%s</a>&gt;' % (i,out)
  elif "development" in reverse[i]: out='(<a href="http://linux.die.net/man/1/%s">%s</a>)' % (i,out)
  elif "request" in reverse[i]: out='<a href="http://linux.die.net/man/1/%s">%s</a>' % (i,out)
  elif "toolbox" in reverse[i]: out='{%s}' % out
  elif "ready" in reverse[i]: pass
  else: sys.stderr.write("unknown %s %s\n" % (i, reverse[i]))

  if "ready" in reverse[i] or "pending" in reverse[i]:
    done.append(out)
    out='<strike>%s</strike>' % out
  else: pending.append(out)

  outfile.write(out+"\n")

outfile.write("</p></blockquote>\n")

outfile.write("<h2>TODO</h2><blockquote><p>%s</p></blockquote>\n" % "\n".join(pending))
outfile.write("<h2>Done</h2><blockquote><p>%s</p></blockquote>\n" % "\n".join(done))
