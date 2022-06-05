/* git.c - A minimal git clone
 *
 * Copyright 2022 Moritz C. Weber <m.c.weber@web.de>
 *
 * See https://git-scm.com/docs/git-init
 * https://git-scm.com/docs/git-remote
 * https://git-scm.com/docs/git-fetch
 * https://git-scm.com/docs/git-checkout
 * https://git-scm.com/docs/pack-format
 * https://git-scm.com/docs/index-format
 * https://www.alibabacloud.com/blog/a-detailed-explanation-of-the-underlying-data-structures-and-principles-of-git_597391
 
USE_GITCLONE(NEWTOY(gitclone, ">1", TOYFLAG_USR|TOYFLAG_BIN))

config GITCOMPAT
  bool "gitcompat"
  default n
  help
    Enable git compatible repos instead of minimal clone downloader.

config GITCLONE
  bool "gitclone"
  default n
  help
    usage: gitclone URL
    A minimal git clone.
	
config GITINIT
  bool "gitinit"
  default n
  help
    usage: gitinit NAME
    A minimal git init.
	
config GITREMOTE
  bool "gitremote"
  default n
  help
    usage: gitremote URL
    A minimal git remote add origin.
	
config GITFETCH
  bool "gitfetch"
  default n
  help
    usage: gitfetch
    A minimal git fetch.
	
config GITCHECKOUT
  bool "gitcheckout"
  default n
  help
    usage: gitcheckout <branch>
    A minimal git checkout.
*/

#define FOR_git
#include TT git
#include "toys.h"

GLOBALS(
  char *url;
  char *name; 
)

struct IndexV2 {
  char header[8]
  long fot[256]
  char *sha1[20]
  long *crc
  long *offset
  long long *64_offset
  char packsha1[20]
  char idxsha1[20]
};
 
static void read_index(FILE *fpi, struct IndexV2 *i){
  fpi=fopen(".git/objects/pack/temp.idx","wb");
  fread(i->header, sizeof(index->header),1,fpi)
  fread(i->fot, sizeof(index->fot),1,fpi);
  fread(i->sha1, sizeof(index->fot),fot[255],fpi);
  fread(i->crc, sizeof(index->fot),fot[255],fpi);
  fread(i->offset, sizeof(index->fot),fot[255],fpi);
  //TODO: Offsets for file size 2G missing here
  if fread(i->packsha1, 20,1,fpi);
  fread(i->idxsha1, 20,1,fpi);
}

void write_children(char *hash, char* path, FILE *fpp){
  FILE* fc;
  
  fseek(fpp,get_index(Index* i,hash),SEEK_SET);
  fread(object, unpack(fpp, type, offset),1,fpp);
  //inflate(object,bb) //TODO: Inflate & Filter
  if (strncmp(object[],"40",4)){//tree
    for each object.subfolders{
	  write_children(child->hash,strcat(strcat(path,"/"),),fpp);
	}
	for each object.files{
	  write_children(child->hash,strcat(strcat(path,"/"),),fpp;
	} 
  } else if (strncmp(object,"100",4)){//file
    fc = fopen(path, "w");
	fwrite(object,1,sizeof(object),fc);
	fclose(fc);
    return;	
  }
}

uint64_t unpack( const uint8_t *const fp, char *type, long *offset)
{
  int i,bitshift,data= 0;
  uint64_t length = 0;
  
  fread(data, 1,1,fpi);
  type=data & 0x70;
  while((data & 0x80) != 0 && fread(data, 1,1,fpi))!=1)
  {
    length |= (uint64_t)(data & 0x7F) << bitshift;     
	bitshift += 7; i++;
  } 
  offset = i;
  return length;
}

void get_index(Index* i, char* []h){
 return i->offset[bsearch(h, i->sha, i->fot[h[0]]-i->fot[h[0]]-1, 20,
   (int(*)(const void*,const void*)) strcmp)];
}

void set_object(struct Index* idx, char* o, long offset){
  char *h;
  //TODO: hash=sha1(o); 
  idx->offset[idx->fot[h[0]]]=offset;
  //ToDo: id->crc[idx->fot[h[0]]]=;
  for (int i=hash[0];i<255;i++)idx->fot[i]]+=1;
  //writeObject to local pack;
}

static void git_init(char *name)
{
  mkdir(name,0755);
  mkdir(xmprintf("%s%s",name,"/.git"),0755);
  mkdir(xmprintf("%s%s",name,".git/objects"),0755);
  mkdir(xmprintf("%s%s",name,".git/objects/pack"),0755);
  mkdir(xmprintf("%s%s",name,"/.git/branches"),0755);
  mkdir(xmprintf("%s%s",name,"/.git/hooks"),0755);//hook files skiped
  mkdir(xmprintf("%s%s",name,"/.git/info"),0755);
  mkdir(xmprintf("%s%s",name,".git/objects/info"),0755);
  mkdir(xmprintf("%s%s",name,".git/refs"),0755);
  mkdir(xmprintf("%s%s",name,".git/heads"),0755);
  mkdir(xmprintf("%s%s",name,".git/tags"),0755);
  xcreate(xmprintf("%s%s",name,".git/config"),O_CREAT,0644);
  xcreate(xmprintf("%s%s",name,".git/description"),O_CREAT,0644);
  xcreate(xmprintf("%s%s",name,".git/HEAD"),O_CREAT,0644);
  xcreate(xmprintf("%s%s",name,".git/info/exclude"),O_CREAT,0644);
}

static void git_remote_add_origin(char *url)
{
  FILE *fp;
  fp=fopen(".git/config","wb");
  fwrite("[core]\n",1,7,fp);
  fwrite("\trepositoryformatversion = 0\n",1,29,fp);
  fwrite("\tfilemode = false\n",1,18,fp);
  fwrite("\tbare = false\n",1,14,fp);
  fwrite("\tlogallrefupdates = true\n",1,25,fp);
  fwrite("\tsymlinks = false\n",1,18,fp);
  fwrite("\tignorecase = true\n",1,19,fp);
  fwrite("[remote \"origin\"]\n",1,18,fp);
  fwrite(xmprintf("%s%s","\turl = %s/refs\n",TT->url),1,sizeof(TT->url)+13,fp);
  fwrite("\tfetch = +ref/heads/*:refs/remotes/origin/*\n",1,44,fp);
  fclose(fp);
}

static void git_fetch(void)
{
  execl("toybox",{"-O" ".git/refs/temp.refs" "https://github.com/landley/toybox/info/refs?service=git-upload-pack"});//TODO: Refactor wget into lib
  execl("toybox",{"wget","-O",".git/objects/pack/temp.pack","-d","-p","$'0032want 8cf1722f0fde510ea81d13b31bde1e48917a0306\n00000009done\n' https://github.com/landley/toybox/git-upload-pack}");//TODO: does not skip 0008NAK
  FILE *fpp,*fpi;
  read_index(fpi,i);
  fpp=fopen(".git/objects/pack/temp.pack","r");
  long *offset;
  long *ocount;
  char *type;
  fsetpos(fpp,16)//header check skipped
  fread(ocount,4,1,fpp)
  while (feof(fpp)){
	fread(&object, unpack(fpp, type, offset),1,fpp);
    set_object(object,ftell(fpp))
  }
  fclose(fpi);
  flcode(fpp);
}

static void git_checkout(char *name)
{
  FILE *fpp,*fh;
  char *hash
  
  fh=fopen(strcat("./ref/heads/",!hash?"master":name),"r");
  fread(hash,20,1,fh);
  fclose(fh);
  fpp=fopen(".git/objects/pack/temp.pack","r");
  write_children(hash,"",ffp);
  fclose(ffp);
}

void git_clone_main(void)
{
  TT.url = xstrdup(toys.optargs[0]));
  if(strend(TT.url,".git")) TT.url[strlen(TT.url)-4]='\0';
  TT.name = strrchr(TT.url,'/')+1:
  git_init(TT.name);
  chdir(TT.name);
  git_remote(TT.url);
  git_fetch();
  git_checkout("master");
  chdir("..");
}

void git_init_main(void)
{
  git_init(strrchr(strtok(xstrdup(toys.optargs[0]),"."),'/'););
}

void git_remote_main(void)
{
  git_remote(xstrdup(toys.optargs[0]));
}

void git_fetch_main(void)
{
git_fetch();
}

void git_checkout_main(void)
{
  TT.name = strtok(xstrdup(toys.optargs[0]),".");
  git_checkout(name);
}

// ./toybox wget -O - -d https://github.com/landley/toybox/info/refs?service=git-upload-pack | less

// ./toybox wget -O pack.dat -d -p $'0032want 8cf1722f0fde510ea81d13b31bde1e48917a0306\n00000009done\n' https://github.com/landley/toybox/git-upload-pack


USE_GITINIT(NEWTOY(gitinit, <1, TOYFLAG_USR|TOYFLAG_BIN))
USE_GITREMOTE(NEWTOY(gitremote, <1, TOYFLAG_USR|TOYFLAG_BIN))
USE_GITFETCH(NEWTOY(gitfetch, 0, TOYFLAG_USR|TOYFLAG_BIN))
USE_GITCHECKOUT(NEWTOY(gitcheckout, <1, TOYFLAG_USR|TOYFLAG_BIN))


long get_index(struct IndexV2 *i, char *h){
 return i->offset[bsearch(h, i->sha1, i->fot[h[0]]-i->fot[h[0]]-1, 20, (int(*)(const void*,const void*)) strcmp)];
}
//USE_GIT_INIT(NEWTOY(git-init, 0, TOYFLAG_USR|TOYFLAG_BIN))
//USE_GIT_REMOTE(NEWTOY(git-remote, 0, TOYFLAG_USR|TOYFLAG_BIN))
//USE_GIT_FETCH(NEWTOY(git-fetch, 0, TOYFLAG_USR|TOYFLAG_BIN))
//USE_GIT_CHECKOUT(NEWTOY(git-checkout, 0, TOYFLAG_USR|TOYFLAG_BIN))
