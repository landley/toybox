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
 * https://github.com/git/git/blob/master/Documentation/technical/pack-format.txt
 * https://stackoverflow.com/a/14303988
 * https://stackoverflow.com/a/21599232
 * https://github.com/tarruda/node-git-core/blob/master/src/js/delta.js


USE_GITCLONE(NEWTOY(gitclone, "<1", TOYFLAG_USR|TOYFLAG_BIN))
USE_GITINIT(NEWTOY(gitinit, "<1", TOYFLAG_USR|TOYFLAG_BIN))
USE_GITREMOTE(NEWTOY(gitremote, "<1", TOYFLAG_USR|TOYFLAG_BIN))
USE_GITFETCH(NEWTOY(gitfetch, 0, TOYFLAG_USR|TOYFLAG_BIN))
USE_GITCHECKOUT(NEWTOY(gitcheckout, "<1", TOYFLAG_USR|TOYFLAG_BIN))

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

#define TT  this.git
#define FOR_gitclone
#include "toys.h"
#include "openssl/sha.h" //ToDo: borrowed from OpenSSL to not pipe or refactor the SHA1SUM in toybox
#include "zlib.h"  //ToDo: borrowed from libz to not refactor deflate.c

GLOBALS(
  char *url, *name;
  struct IndexV2 *i;
)

struct IndexV2 { //git inxed format v2 https://github.com/git/git/blob/master/Documentation/technical/pack-format.txt#L241
  char header[8];
  uint32_t fot[256];
  char (*sha1)[20];
  uint32_t *crc;
  uint32_t *offset;
  long long *offset64;//not supported yet
  char packsha1[20];
  char idxsha1[20];
};

static void read_index(struct IndexV2 *i)
{
  FILE *fpi;
  i=malloc(sizeof(i));
  //i->fot={ 0 };
  i->sha1=malloc(20*sizeof(char));
  i->crc=malloc(sizeof(uint32_t));
  i->offset=malloc(sizeof(uint32_t));
  i->offset64=malloc(sizeof(long long));
  //TODO: not used yet as index is not persisted yet
  if (access(".git/object/pack/temp.idx", F_OK)==0) {
    //persistance needed for other git commands (not clone)
    fpi=fopen(".git/object/pack/temp.idx", "rb");
    printf("read header\n");
    fread(i->header, sizeof(i->header), 1, fpi);
    printf("Header: %s..Read fot\n", i->header);
    fread(i->fot, 4, 256, fpi);
    printf("Elements %d..Read sha1\n", i->fot[255]);
    fread(i->sha1, sizeof(i->fot), i->fot[255], fpi);
    printf("read crc\n");
    fread(i->crc, sizeof(i->fot), i->fot[255], fpi);
    printf("read offset\n");
    fread(i->offset, sizeof(i->fot), i->fot[255], fpi);
    //TODO: Offsets for file size 2G missing here
    printf("read packsha\n");
    fread(i->packsha1, 20, 1, fpi);
    printf("read idxsha\n");
    fread(i->idxsha1, 20, 1, fpi);
    fclose(fpi);
  }
}

static char *l; //for saving the insertion position

int cmp (const void *i, void *j)
{
  l=j; //inject inseration position in compare to binary search
  //printf("Compare %p %p %d\n",i,j,strncmp(i,j,20));
  return strncmp(i,j,20);
}

//inspired by musl bsearch
long bsearchpos(const void *k, const void *a,size_t h, size_t w)
{
  long l = 0, m = 0, r = 0;
  if (!h) return 0;
  //printf("Array: %p Key:%p\n",a,k);
  while(h>0){
    m=l+(h/2);
    //m=(l+h)/2;
    //printf("l: %ld m:%ld h:%ld\n",l,m,h);
    r=strncmp(k,a+(m*w),20);
    //printf("r: %ld\n",r);
    if(!r||h==1)break;//match on search or position for insert
    if(r<0){h/=2;}else{l=m;h-=h/2;}
    //if(r<0){h=m-1;}else{l=m+1;}
  }
  //printf("Return m: %ld r:%ld \n",m,r);

  //For inserts check if insert is bigger  obj at identified position
  return m += (r>0) ? 1 : 0;
}

long get_index(struct IndexV2 *i, char *h)
{
  long pos=bsearchpos(h, i->sha1[0], i->fot[255], 20); //,

  //(int(*)(const void*,const void*)) cmp);//TODO: Should be placed by bsearchpos() below; cmp and *l to be removed too
  // for (int j=0;j<20;j++) printf("%02x",h[j]);
  //    printf("\n");
  //if (pos == NULL){
  // for (int h=0;h<i->fot[255];h++){
  // printf("%d: ",h);
  // for (int j=0;j<20;j++) printf("%02x",i->sha1[h][j]);
  //   printf("\n");
  // }
  //}
  // printf("index pointer: %ld\n",pos);
  // printf("fot[255]: %d\n",i->fot[255]);
  // printf("sha1[0] pointer: %p\n",i->sha1[0]);
  // printf("offset index : %ld\n",pos);
  // return i->offset[(pos-i->sha1[0])/20];

 return i->offset[pos];
}

//https://github.com/git/git/blob/master/Documentation/technical/pack-format.txt#L30
//https://yqintl.alicdn.com/eef7fe4f22cc97912cee011c99d3fe5821ae9e88.png

//read type and lenght of an packed object
uint64_t unpack(FILE *fpp, int *type, long int *offset)
{
  int bitshift= 4;
  uint64_t length = 0;
  uint8_t data;

  printf("Start unpack\n");
  fseek(fpp,*offset,SEEK_SET);
  printf("Offset set to: %ld\n", *offset);
  fread(&data, 1, 1, fpp);
  printf("Data: %d\n", data);
  *type=((data & 0x70)>>4);
  printf("Type: %d\n",*type);
  length |= (uint64_t)(data & 0x0F);
  //(*offset)++;
  while((data & 0x80) && fread(&data, 1, 1, fpp)!=-1)
  {
    length |= (uint64_t)(data & 0x7F) << bitshift;
    bitshift += 7; // (*offset)++;
    //printf("Offset set to: %ld\n",*offset);
  }
  //printf("Offset set to: %ld\n",*offset);
  printf("Length: %ld\n", length);
  return length;
}

//   ToDo: borrowed from int inf(FILE *source, FILE *dest) in
//   zpipe.c: example of proper use of zlib's inflate() and deflate()
//   Not copyrighted -- provided to the public domain
//   Version 1.4  11 December 2005  Mark Adler */
#define CHUNK 4096
int inf(FILE *source, char *dest) //modified signature to ease use
{
    int ret;
    char *position=dest;
    unsigned have;
    z_stream strm;
    unsigned char in[CHUNK];
    unsigned char out[CHUNK];
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = 0;
    strm.next_in = Z_NULL;
    ret = inflateInit(&strm);
    if (ret != Z_OK)
        return ret;

    // decompress until deflate stream ends or end of file
    do {

        strm.avail_in = fread(in, 1, CHUNK, source);
        if (ferror(source)) {
            (void)inflateEnd(&strm);
            return Z_ERRNO;
        }
        if (strm.avail_in == 0)
            break;
        strm.next_in = in;


        // run inflate() on input until output buffer not full
        do {

            strm.avail_out = CHUNK;
            strm.next_out = out;

            ret = inflate(&strm, Z_NO_FLUSH);
            //assert(ret != Z_STREAM_ERROR);  // state not clobbered
            switch (ret) {
            case Z_NEED_DICT:
                ret = Z_DATA_ERROR;     // and fall through
            case Z_DATA_ERROR:
            case Z_MEM_ERROR:
                (void)inflateEnd(&strm);
                return ret;
            }

            have = CHUNK - strm.avail_out;
	    memcpy(position,out,have);//added to original
            position+=have;//added to original
            //if (fwrite(out, 1, have, dest) != have || ferror(dest)) {
            //    (void)inflateEnd(&strm);
            //    return Z_ERRNO;
            //}
        } while (strm.avail_out == 0);
       // done when inflate() says it's done
    } while (ret != Z_STREAM_END);
    fseek(source,ftell(source)-strm.avail_in,SEEK_SET);//modified from zpipe.c to set FP to end of zlib object
    // clean up and return
    (void)inflateEnd(&strm);
    return ret == Z_STREAM_END ? Z_OK : Z_DATA_ERROR;
}

//https://github.com/git/git/blob/master/Documentation/gitformat-pack.txt#L72
long set_object(struct IndexV2 *idx,int type, char *o, uint32_t count,
  uint32_t ofs)
{
// TODO: Too many allocs in here 1) to concat the search string for hashing
// 2) to insert into the array (can be reduce to a single malloc in fetch as
// the pack header contains the number of objects in pack
  char *c,*p="",*h=(char*)xmalloc(sizeof(char)*20);//composition,prefix,hash
  long pos=0;

  printf("Alloc... ");
  if (h == NULL) error_exit("Hash malloc failed in set_object");
  switch(type) {
    case 1:p=xmprintf("commit %d",count);break;//count is used as o can contain \0 in the  string
    case 2: p=xmprintf("tree %d",count); break;
    case 3: p=xmprintf("blob %d",count); break;
    case 4: p=xmprintf("tag %d",count); break;
    case 6: printf("REF_DELTA"); break; //not expected in fetch packs as fetch packs are self-containing
    case 7: printf("OBJ_DELTA\n"); break;
  }
  c = (char*)xmalloc(strlen(p)+count+2); //Robs null terminator embedding
  if (c == NULL) error_exit("c malloc failed in set_object");
  memcpy(c,p,strlen(p)+1); //Robs null terminator embedding
  memcpy(c+strlen(p)+1,o,count+1); //Robs null terminator embedding
  //printf("Enriched Object: %s %ld\n",c,strlen(p)+count+2);
  h = SHA1(c,strlen(p)+count+1,h); //ToDo: borrowed from OpenSSL to not to pipe or refactor SHA1SUM in toybox
  printf("..Binary search\n");
  //printf("\nidx->fot[255]=%d\n",idx->fot[255]);
  //printf("idx->sha1[fot[h[0]]]=%d\n",sizeof(idx->sha1));
  //TODO:Array Insert broken
  //if (idx->fot[255]>1)
  //{
  //  printf("Bsearch result: %p\n",bsearch(h, idx->sha1[0], idx->fot[255], 20,
  //  (int(*)(const void * ,const void *)) cmp));//find insertation position
  //  printf("Inseration position pointer %p\n",l);
  //  pos=(long)(((l-idx->sha1[0])/20)+((strncmp(h,l,20)<0)?0:1));//ugly ins pos hack
  //  printf("Bigger one %ld\n",(long)(((l-idx->sha1[0])/20)+((strncmp(h,l,20)<0)?0:1)));
  //} else {
  //  printf("Smaller two\n");
  //  if (idx->fot[255]==0)
  //  {
  //    pos=0;
  //  }else{
  //    pos=(strncmp(h,idx->sha1[0],20)<0)?0:1;
  //  }
  // }
  //printf("Binary search position: %ld, %p %p\n",pos,idx->sha1[0],l);
  //l=NULL;
  for (int j = 0; j<20; j++) printf("%02x",h[j]); //find insert position
  pos = bsearchpos(h,idx->sha1[0],idx->fot[255],20);
  printf("\n..Insert pos %ld\n",pos);
  printf("..Preloop\n");
  for (int i = h[0]; i<=255; i++)
    idx->fot[i]+=1; //adjust of fanout table https://github.com/git/git/blob/master/Documentation/technical/pack-format.txt#L179
  printf("Post loop\n");
  printf("Resize sha1 array..idx->fot[255]%d\n",idx->fot[255]); //Memory management for insert TODO:Could be also a single malloc at gitfetch based on the nbr of objects in pack

  //Did not fix the TODO yet, because set_object could be reused for other command im mem mgmt is here
  idx->sha1 = realloc(idx->sha1,(idx->fot[255]+1)*20*sizeof(char));
  printf("Mem copy sha1 array..sizeof(idx->sha1)%ld\n",sizeof(idx->sha1));
  memmove(&idx->sha1[pos+1],&idx->sha1[pos],(idx->fot[255]-pos)*20*sizeof(char));
  printf("Resize offset\n");
  idx->offset = realloc(idx->offset, (idx->fot[255]+1)*sizeof(uint32_t));
  printf("Mem copy offset\n");
  memmove(&idx->offset[pos+1], &idx->offset[pos], sizeof(uint32_t)*(idx->fot[255]-pos));
  printf("Set offset value\n");
  memcpy(&idx->sha1[pos], h, 20);//insert SHA1
  idx->offset[pos] = ofs;//insert offset of SHA1
  //ToDo: id->crc[idx->fot[h[0]]]=;
  printf("Write object\n");
  // printf("SetGet %d %ld\n :",idx->offset[pos],get_index(idx,h));
  //writeObject to local pack;
  //    for (int h=0;h<idx->fot[255];h++){
  //      for (int j=0;j<20;j++) printf("%02x",idx->sha1[h][j]);
  //        printf("\n");
  //    }
  free(h);
  free(c);

  return ofs;
}

static void gitinit(char *name)
{
  //For git clone actually only refs and object/pack needed
  if (mkdir(name,0755)!=0){
    mkdir(xmprintf("%s%s", name, "/.git"), 0755);//I create the other for a git compliant folder structure
    mkdir(xmprintf("%s%s", name, "/.git/objects"), 0755);
    mkdir(xmprintf("%s%s", name, "/.git/objects/pack"), 0755);
    mkdir(xmprintf("%s%s", name, "/.git/branches"), 0755);
    mkdir(xmprintf("%s%s", name, "/.git/hooks"), 0755);//hook files skipped as implementations does not support hooks
    mkdir(xmprintf("%s%s", name, "/.git/info"), 0755);
    mkdir(xmprintf("%s%s", name, "/.git/objects/info"), 0755);
    mkdir(xmprintf("%s%s", name, "/.git/refs"), 0755);
    mkdir(xmprintf("%s%s", name, "/.git/heads"), 0755);
    mkdir(xmprintf("%s%s", name, "/.git/tags"), 0755);
    xcreate(xmprintf("%s%s", name, "/.git/config"), O_CREAT, 0644);
    xcreate(xmprintf("%s%s", name, "/.git/description"), O_CREAT, 0644);
    xcreate(xmprintf("%s%s", name, "/.git/HEAD"), O_CREAT, 0644);
    xcreate(xmprintf("%s%s", name, "/.git/info/exclude"), O_CREAT, 0644);
  }
}

static void gitremote(char *url)
{
  if (access(".git/config", F_OK)!=0) {
    FILE *fp = fopen(".git/config","wb");

    fwrite("[core]\n", 1, 7, fp);
    fwrite("\trepositoryformatversion = 0\n", 1, 29, fp);
    fwrite("\tfilemode = false\n", 1, 18, fp);
    fwrite("\tbare = false\n", 1, 14, fp);
    fwrite("\tlogallrefupdates = true\n", 1, 25, fp);
    fwrite("\tsymlinks = false\n", 1, 18, fp);
    fwrite("\tignorecase = true\n", 1 ,19, fp);
    fwrite("[remote \"origin\"]\n", 1 ,18, fp);
    fwrite(xmprintf("\turl = %s/refs\n",TT.url), 1, strlen(TT.url)+13, fp);
    fwrite("\tfetch = +ref/heads/*:refs/remotes/origin/*\n", 1, 44, fp);
    fclose(fp);
  }
}

//this is most likely still buggy and create a late observable heap overflow larger deltafied repos
char* resolve_delta(char *s, char *d, long dsize, uint32_t *count)
{ //https://stackoverflow.com/a/14303988
  long pos=0, bitshift=0;

  printf("Original Source: \n");
  //for (int k=0;k<*count;k++){printf("%c",s[k]);}
  //printf("\n");
  //printf("Delta:\n");
  //for (int k=0;k<dsize;k++){printf("%c",d[k]);}
  //printf("\n");

  //https://github.com/git/git/blob/master/Documentation/technical/pack-format.txt#L88
  while((d[pos] & 0x80)) pos++; // Skipping source size; did not find out why it is  on the delta header as the source object header contains it too; maybe misunderstood and this makes things buggy, but I dont need it here
  //{
  // ssize |= (uint64_t)(d[pos++] & 0x7F) << bitshift;
  // bitshift += 7;// (*offset)++;
  //}
  pos++; //fixes https://github.com/git/git/blob/master/Documentation/technical/pack-format.txt#L67
  *count = 0;
  bitshift = 0;
  while((d[pos] & 0x80)) { //reading target_size from header
    *count |= (uint64_t)(d[pos++]& 0x7F) << bitshift;
    bitshift += 7; // (*offset)++;
  }

  *count |= (uint64_t)(d[pos++]& 0x7F) << bitshift;
  printf("Target Count %d:\n", *count);
  char *t = malloc(sizeof(char)*(*count+1));
  if (t == NULL) error_exit("t malloc failed in resolve_delta");
  *count = 0;
  while (pos<dsize) {
    int i = 0, j = 1;
    uint32_t offset = 0, size = 0;

    //printf("d[pos]: %d %ld\n",d[pos],pos);
    if ((d[pos]&0x80)) {//https://github.com/git/git/blob/master/Documentation/technical/pack-format.txt#L103
    //https://stackoverflow.com/a/14303988
      printf("Case 1\n");
      while (i<4) {
        //printf("Offset: %d i: %d j: %d d[pos+j]: %d \n",offset,i,j,d[pos+j]);
        if (d[pos]&(1<<i)) {
          offset |= d[pos+j]<<(i*8);
          j++;
        }
        i++;
      }
      //printf("Offset: %d \n",offset);
      while(i<7) {
        //printf("Size: %d i: %d j: %d d[pos+j]: %d \n",size,i,j,d[pos+j]);
        if (d[pos]&(1<<i)) {
          size |= d[pos+j]<<((i+4)*8);
          j++;
        }
        i++;
      }

      // packfomat: size zero is automatically converted to 0x10000.
      // https://github.com/git/git/blob/master/Documentation/technical/pack-format.txt#L133
      if (size==0) size=0x10000;
//     printf("Size %d\n",size);
//      printf("Realloc\n");
      //printf("Memcpy %s %d %d\n", t, size, offset);
      memcpy(t+*count,s+offset, size);
      //t[size] = '\0';
      pos += j;
      printf("Pos\n");
    } else {
      //https://github.com/git/git/blob/master/Documentation/technical/pack-format.txt#L133
//    printf("Case 0\n");
      size=d[pos++];//incrememt
//    printf("Memcopy %d\n",size);
      memcpy(t+*count,d+pos,size);
//    printf("Pos %ld\n",pos);
      pos+=size;
    }
    *count+=size;

    printf("Target: \n");
//  for (int k=0;k<*count;k++){printf("%c",t[k]);}
//  printf("\n");
  }
  free(s);
  free(d);

  return t;
}

char* unpack_object(FILE *fpp, struct IndexV2 *i, long offset, uint32_t *count,
  int *type)
{
  uint32_t dcount = unpack(fpp,type,&offset);
  char *object = malloc((sizeof(char)*(dcount)+1));

  if (object == NULL) error_exit("object malloc failed in unpack_object");
  object[*count] = '\0';
  printf("Count: %d \n",*count);
// see OBJ_REF_DELTA here https://yqintl.alicdn.com/eef7fe4f22cc97912cee011c99d3fe5821ae9e88.png
  if (*type==7) {
// printf("Type 7:\n");
      char *h = malloc(20*sizeof(char));

      if (h == NULL) error_exit("h malloc failed in unpack_object");
      fread(h, 20, 1, fpp); //fseek(fpp,20,SEEK_CUR);
      printf("Read base object\n");
      for (int j = 0; j<20; j++) printf("%02x",h[j]);
      printf("\n");
      inf(fpp, object); //TO CHECK IF INF OR PLAIN TEXT:
      long int toffset = ftell(fpp);//save original file offset
      char *source = unpack_object(fpp, i, get_index(i, h), count, type);
      printf("Inflate delta data\n");
      fseek(fpp, toffset, SEEK_SET);//return to original file offset
      printf("Resolve delta data\n");
      //printf("Print source: %s\n", source);
      //for (int h = 0; h<i->fot[255]; h++){
      //  for (int j = 0; j<20; j++) printf("%02x", i->sha1[h][j]);
      //  printf("\n");
      //}
      free(h);

      // recursion due to https://github.com/git/git/blob/master/Documentation/technical/pack-format.txt#L83
      return resolve_delta(source,object,dcount,count);
    } else {
      printf("Type Else:\n");
      inf(fpp, object);
      *count = dcount;
      printf("Type Else end:\n");
      //printf("Unpacked Object: %s\n", object);

      return object;
    }
}

char* txtoh(char *p){ //make 20byte SHA1 hash from 40 byte SHA1 string
//  printf("txtoh start");
  char *h=malloc(sizeof(char)*41); //TODO: Dont like the malloc here, but did not find a solution to sscanf into p again
  if (h == NULL) error_exit("h malloc failed in unpack_object");
  for(int c=0; c<20;c++){
 // printf("c: %d\n",c);
  sscanf(&p[2*c],"%2hhx",&(h[c]));
  }
//  printf("txtoh end");
  h[20]='\0';
  printf("return");
  return h;
}

//traveres the commit tree for checkout
void write_children(char *hash, char *path, FILE *fpp){
  FILE *fc;
  char *object;
  int type;
  long int offset;
  uint32_t count;
  //printf("process hash: ");
  //      for (int j=0;j<20;j++) printf("%02x", hash[j]);
  //        printf("\n");
  printf("seek index\n");

  offset= get_index(TT.i,hash);
  printf("Found index: %ld\n",offset);
  //fseek(fpp,offset,SEEK_SET);
  printf("read object\n");
 // size_t size=unpack(fpp,&type,&offset);
 // printf("Size: %ld \n",size);
 // printf("Size2: %ld \n",size);
  object=unpack_object(fpp,TT.i,offset,&count,&type);
  printf("%s\n",object);
  printf("Type %d\n",type);
  if(type==1){//at commit object
    memcpy(hash,&object[5],40);
    write_children(txtoh(hash),path,fpp);
  }else if(type==2){//at tree object https://stackoverflow.com/a/21599232
    char *hs=0;
    int pos=0;
    printf("process folder %s\n",path);
    while (pos<count){
      hs=strchr(object+pos,'\0')+1;//find position where the next hash starts
      printf("Object+pos: %s\n",object+pos);
      char *name;//=malloc(sizeof(char)*(hs-(object+pos))+strlen(path));
      //memcpy(mode,object+pos+2,3)//TODO:String to umask
      if (*(object+pos)=='1'){//tree object reference is a file
      name=(strlen(path)>0)?xmprintf("%s/%s",path,object+pos+7):object+pos+7;//concat file name
        printf("prepare file %s\n",name);
      }else{//tree object reference is a folder
        name=(strlen(path)>0)?xmprintf("%s/%s",path,object+pos+6):object+pos+6;//concat folder name
        printf("create folder %s\n",name);
        mkdir(name,0755);//TODO: umask
      }
      memcpy(hash,hs,20);
      write_children(hash,name,fpp);
      //free(name);
      pos=hs-object+20;
      printf("Position/count for %s: %d/%u\n",path,pos,count);
    }
    printf("**EXIT WHILE**\n");
  }else{//at blob/file object
   printf("process file %s\n",path);
    fc = fopen(path, "w");
   printf("process opened \n");
	fputs(object,fc);//TODO:Not sure if length might be an issue here
   printf("process file written\n");
	fclose(fc);
  }
free(object);
printf("Child: %s done\n",path);
}

static void gitfetch(void)
{
  //size_t l=0;
  printf("refs\n");
  pid_t pid; //TODO:I use herein after two temp files for fetch which git due not offer to 1) avoid a rewrite and 2) messing up the repo files while testing
  if ((pid=fork())==0)execv("toybox",(char *[]){"toybox","wget","-O",".git/refs/temp.refs","https://github.com/landley/toybox/info/refs?service=git-upload-pack",(char*)0});//TODO: Refactor wget into lib
  perror("execv\n");
  char h[]="8cf1722f0fde510ea81d13b31bde1e48917a0306";
//  char h[]="52fb04274b3491fdfe91b2e5acc23dc3f3064a86";//TODO: Replace static testing hash and uncomment the following line if rare delta resolve /?heap overflow? bug was found
  //FILE *fpr;
  //fpr=fopen(".git/ref/temp.refs","r");
  //fseek();
  //getline(&h,&l,fpr);
  //getline(&h,&l,fpr);
  //getline(&h,&l,fpr);
  //fclose(fpr);
  //strcpy(h,&h[4],4);
  //h[40]='\0';
  printf("pack\n");
  //if ((pid=fork())==0)execv("toybox",(char *[]){"toybox","wget","-O",".git/objects/pack/temp.pack","-p","$'0032want 52fb04274b3491fdfe91b2e5acc23dc3f3064a86\n00000009done\n'","https://github.com/landley/toybox/git-upload-pack",(char*)0});//TODO: does not skip 0008NAK  printf("init\n");
  if ((pid=fork())==0)execv("toybox",(char *[]){"toybox","wget","-O",".git/objects/pack/temp.pack","-p",xmprintf("$'0032want %s\n00000009done\n'",h),"https://github.com/landley/toybox/git-upload-pack",(char*)0});//TODO: does not skip 0008NAK  printf("init\n");
  perror("execv\n");
  FILE *fpp;
  printf("openpack\n");
  fpp=fopen(".git/objects/pack/temp.pack","r");
  printf("read index\n");
  read_index(TT.i);//init index with out reading
  printf("init\n");
  uint32_t ocount=0, count;
  int type;
  char *object;
  printf("skip header\n");
  long int offset=12+8;//8byte from the wget post response are skipped too
  fseek(fpp,8+8,SEEK_SET);//header check skipped https://github.com/git/git/blob/master/Documentation/technical/pack-format.txt#L14
  printf("read count\n");
  fread(&ocount,4,1,fpp);
  ocount=ntohl(ocount);//https://github.com/git/git/blob/master/Documentation/technical/pack-format.txt#L21
  printf("Count: %d ..Loop pack\n",ocount);
  for (int j=0;j<ocount;j++){
    printf("Read object %d\n",j);
    count=0;
    object=unpack_object(fpp,TT.i,offset,&count,&type);
    printf("Count: %d Offset: %ld  ..Set object\n",count,offset);
    //if(j>5126)printf("SetGetObject %d: %s\n",j,object);
    //printf("SetGetUnpack %d: %s\n",j,unpack_object(fpp,TT.i,set_object(TT.i,type,object,count,offset),&count,&type));
    set_object(TT.i,type,object,count,offset);

    free(object);

    printf("Adjust offset\n");
    offset=ftell(fpp);//adjust offset to new file position
    printf("Adjusted offset to: %ld\n",offset);
  }
//  for (int h=0;h<TT.i->fot[255];h++){
//  printf("%d: ",h);
//  for (int j=0;j<20;j++) printf("%02x",TT.i->sha1[h][j]);
//    printf("\n");
//  }

  //TODO: Final pack checksum not calculated and checked
  fclose(fpp);
}

static void gitcheckout(char *name)
{

  FILE *fpp;
  //FILE *fh;
  printf("Find branch for checkout\n");
  //fh=fopen(xmprintf(".git/ref/heads/%s",name?"master":name),"r");//TODO: Checkout master as in ref/heads
  printf("Read head\n");
  //hf[fread(&hf,40,1,fh)]='\0';
  //fclose(fh);
  printf("Close heads and read pack\n");
  fpp=fopen(".git/objects/pack/temp.pack","r");
  printf("set signature\n");
  char *p="52fb04274b3491fdfe91b2e5acc23dc3f3064a86";//static hashes for testing toybox 0.0.1";
  //char *p="c555a0ca46e75097596274bf5e634127015aa144";//static hashes for testing 0.0.2";
  //char *p="4307a7b07cec4ad8cbab47a29ba941f8cb041812";//static hashes for testing 0.0.3";
  //char *p="3632d5d8fe05d14da983e37c7cd34db0769e6238";//static hashes for testing 0.0.4";
  //char *p="8cf1722f0fde510ea81d13b31bde1e48917a0306";//3604ba4f42c3d83e2b14f6d0f423a33a3a8706c3";
  printf("enter tree root\n");
  write_children(txtoh(p),"",fpp);
  fclose(fpp);
}

void gitclone_main(void)
{
  TT.url = xstrdup(toys.optargs[0]);
  if(strend(TT.url,".git")) TT.url[strlen(TT.url)-4]='\0';
  TT.name = strrchr(TT.url,'/')+1;
  gitinit(TT.name);
  chdir(TT.name);
  TT.i=malloc(sizeof(struct IndexV2));
  gitremote(TT.url);
  gitfetch();
  gitcheckout("master");
  chdir("..");
  return;
}

#define FOR_gitinit
#include "generated/flags.h"

void gitinit_main(void)
{
  gitinit(xstrdup(toys.optargs[0]));
}

#define FOR_gitremote

void gitremote_main(void)
{
  TT.url = xstrdup(toys.optargs[0]);
  if(strend(TT.url,".git")) TT.url[strlen(TT.url)-4]='\0';
  gitremote(TT.url);
}

#define FOR_gitinit

void gitfetch_main(void)
{
  gitfetch();
}

#define FOR_gitcheckout

void gitcheckout_main(void)
{
  gitcheckout(xstrdup(toys.optargs[0]));
}


// ./toybox wget -O - -d https://github.com/landley/toybox/info/refs?service=git-upload-pack | less

// ./toybox wget -O pack.dat -d -p $'0032want 8cf1722f0fde510ea81d13b31bde1e48917a0306\n00000009done\n' https://github.com/landley/toybox/git-upload-pack
