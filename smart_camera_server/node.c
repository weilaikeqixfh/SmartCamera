#include"node.h"
#include<stdlib.h>
#include<string.h>

extern Node *head;

int InitLink()
{
	head = (Node *)malloc(sizeof(Node));
	if(NULL==head)
	{
		return -1;
	}
	head->next = NULL;
	return 0;	

}
//尾插法
int InsertLink(const char * id, int fd)
{
	if(NULL==id)
	{
		return -1;
	}

	Node *n = (Node *)malloc(sizeof(Node));
	if(NULL == n)
	{
		return -1;
	}

	strcpy(n->deviceid,id);
	n->fd = fd;
	n->next = NULL;

	Node *p =head;
	while(p->next)
	{
		p=p->next;
	}

	p->next=n;
	return 0;
}

int TraverseLink(const char *id)
{
	if(NULL==id)
	{
		return -1;
	}
	
	Node *p = head->next;
	while(p)
	{
		if(!strcmp(p->deviceid,id))
		{
			return p->fd;
		}
		p = p->next;
	}
	return -1;

}
