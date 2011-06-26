#include <u.h>
#include <libc.h>
#include <thread.h>
#include "dat.h"
#include "fns.h"

char*
nodepath(char *s, char *e, Revnode *nd)
{
	static char *frogs[] = {
		"con", "prn", "aux", "nul",
		"com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8", "com9",
		"lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9",
	};
	char *p;
	int i;

	if(nd == nil || nd->name == nil)
		return s;

	s = seprint(nodepath(s, e, nd->up), e, "/");

	p = nd->name;
	for(i=0; i<nelem(frogs); i++)
		if(strcmp(frogs[i], p) == 0)
			return seprint(s, e, "%.2s~%.2x%s", p, p[2], p+3);

	for(; s+4 < e && *p; p++){
		if(*p == '_'){
			*s++ = '_';
			*s++ = '_';
		} else if(*p >= 'A' && *p <= 'Z'){
			*s++ = '_';
			*s++ = 'a' + (*p - 'A');
		} else if(*p >= 126 || strchr("\\:*?\"<>|", *p)){
			*s++ = '~';
			s = seprint(s, e, "%.2x", *p);
		} else
			*s++ = *p;
	}
	*s = 0;

	return s;
}

static void
addnode(Revnode *d, char *path, uchar *hash)
{
	char *slash, *x;
	Revnode *c, *p;

	while(path && *path){
		if(slash = strchr(path, '/'))
			*slash++ = 0;
		p = nil;
		for(c = d->down; c; p = c, c = c->next)
			if(strcmp(c->name, path) == 0)
				break;
		if(c == nil){
			c = malloc(sizeof(*c) + (!slash ? HASHSZ : 0) + strlen(path)+1);
			c->path = 1;
			x = (char*)&c[1];
			if(!slash){
				memmove(c->hash = (uchar*)x, hash, HASHSZ);
				x += HASHSZ;
			} else
				c->hash = nil;
			strcpy(c->name = x, path);
			c->up = d;
			c->down = nil;
			if(p){
				c->next = p->next;
				p->next = c;
			} else {
				c->next = d->down;
				d->down = c;
			}

			if(c->hash){
				p = c;
				p->path = *((uvlong*)c->hash);
				while(d->up){
					d->path += p->path;
					p = d;
					d = d->up;
				}
			}
		}
		d = c;
		path = slash;
	}
}

typedef struct Hashstr Hashstr;
struct Hashstr
{
	Hashstr	*next;
	char	str[];
};

static ulong
hashstr(char *s)
{
	ulong h, t;
	char c;

	h = 0;
	while(c = *s++){
		t = h & 0xf8000000;
		h <<= 5;
		h ^= t>>27;
		h ^= (ulong)c;
	}
	return h;
}

static int
loadmanifest(Revnode *root, int fd, Hashstr **ht, int nh)
{
	char buf[BUFSZ], *p, *e;
	uchar hash[HASHSZ];
	int n;

	p = buf;
	e = buf + BUFSZ;
	while((n = read(fd, p, e - p)) > 0){
		p += n;
		while((p > buf) && (e = memchr(buf, '\n', p - buf))){
			*e++ = 0;

			strhash(buf + strlen(buf) + 1, hash);
			if(ht == nil)
				addnode(root, buf, hash);
			else {
				Hashstr *he;

				for(he = ht[hashstr(buf) % nh]; he; he = he->next){
					if(strcmp(he->str, buf) == 0){
						addnode(root, buf, hash);
						break;
					}
				}
			}

			p -= e - buf;
			if(p > buf)
				memmove(buf, e, p - buf);
		}
		e = buf + BUFSZ;
	}
	return 0;
}

static Revtree*
loadtree(Revlog *manifest, Revinfo *ri, Hashstr **ht, int nh)
{
	Revtree *t;
	int fd;

	if((fd = revlogopentemp(manifest, hashrev(manifest, ri->mhash))) < 0)
		return nil;

	t = malloc(sizeof(*t));
	memset(t, 0, sizeof(*t));
	incref(t);

	t->info = ri;

	t->root = malloc(sizeof(Revnode));
	t->root->path = 0;
	t->root->name = 0;
	t->root->up = nil;
	t->root->down = nil;
	t->root->next = nil;
	t->root->hash = nil;

	if(loadmanifest(t->root, fd, ht, nh) < 0){
		close(fd);
		closerevtree(t);
		return nil;
	}

	close(fd);

	return t;
}

Revtree*
loadfilestree(Revlog *, Revlog *manifest, Revinfo *ri)
{
	return loadtree(manifest, ri, nil, 0);
}

Revtree*
loadchangestree(Revlog *changelog, Revlog *manifest, Revinfo *ri)
{
	char buf[BUFSZ], *p, *e;
	Hashstr *ht[256], *he, **hp;
	int fd, done, line, n;
	Revtree *t;

	if((fd = revlogopentemp(changelog, hashrev(changelog, ri->chash))) < 0)
		return nil;

	done = 0;
	line = 0;
	memset(ht, 0, sizeof(ht));

	p = buf;
	e = buf + BUFSZ;
	while((n = read(fd, p, e - p)) > 0){
		p += n;
		while((p > buf) && (e = memchr(buf, '\n', p - buf))){
			*e++ = 0;

			if(++line >= 4){
				if(*buf == 0){
					done = 1;
					break;
				}

				he = malloc(sizeof(*he) + strlen(buf)+1);
				hp = &ht[hashstr(strcpy(he->str, buf)) % nelem(ht)];
				he->next = *hp;
				*hp = he;
			}

			p -= e - buf;
			if(p > buf)
				memmove(buf, e, p - buf);
		}
		if(done)
			break;
		e = buf + BUFSZ;
	}
	close(fd);

	t = loadtree(manifest, ri, ht, nelem(ht));

	for(hp = ht; hp != &ht[nelem(ht)]; hp++){
		while(he = *hp){
			*hp = he->next;
			free(he);
		}
	}

	return t;
}

static void
freenode(Revnode *nd)
{
	if(nd == nil)
		return;
	freenode(nd->down);
	freenode(nd->next);
	free(nd);
}

void
closerevtree(Revtree *t)
{
	if(t == nil || decref(t))
		return;
	freenode(t->root);
	free(t);
}