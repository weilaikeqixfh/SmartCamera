#ifndef NODE_H
#define NODE_H

struct Node
{
	int fd;
	char deviceid[10];
	struct Node * next;

};

typedef struct Node Node;

int InitLink();

int InsertLink(const char*id,int fd);

int TraverseLink(const char* id);

#endif
