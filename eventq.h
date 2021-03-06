#ifndef _EVENTQ_H
#define _EVENTQ_H

#include <stdio.h>
#include "basic.h"
#include "queue.h"

void fprint_event(FILE * stream, void *p);
void fprint_clet(FILE * stream, void *p);
void fprint_cloud(FILE * stream, void *p);
int time_cmp(void *xp, void *yp);
int event_cmp(void *xp, void *yp);
struct event *dequeue_event(struct queue_t *queue);
struct event *get_head_event(struct queue_t *queue);
void enqueue_event(struct event *e, struct queue_t *queue);
struct event *alloc_event();
struct event *remove_event(struct queue_t *queue, struct event *e, unsigned int n);

#endif /* _EVENTQ_H */
