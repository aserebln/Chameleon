/*
 * Copyright (c) 2005 Apple Computer, Inc.  All Rights Reserved.
 */

#ifndef __DEVICE_TREE_H
#define __DEVICE_TREE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct _Property {
    char *             name;
    uint32_t           length;
    void *             value;

    struct _Property * next;
} Property;

typedef struct _Node {
    struct _Property * properties;
    struct _Property * last_prop;
    
    struct _Node *     children;

    struct _Node *     next;
} Node;


extern Property *
DT__AddProperty(Node *node, char *name, uint32_t length, void *value);

extern Node *
DT__AddChild(Node *parent, char *name);

Node *
DT__FindNode(char *path, bool createIfMissing);

extern void
DT__FreeProperty(Property *prop);

extern void
DT__FreeNode(Node *node);

extern char *
DT__GetName(Node *node);

void
DT__Initialize(void);

/*
 * Free up memory used by in-memory representation
 * of device tree.
 */
extern void
DT__Finalize(void);

void
DT__FlattenDeviceTree(void **result, uint32_t *length);


#endif /* __DEVICE_TREE_H */
