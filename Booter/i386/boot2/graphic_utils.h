 /* Graphic utility functions and data types
  * Prashant Vaibhav (C) 12/12/2008
  * Chameleon
  */

// Everything here is 32 bits per pixel non-premultiplied ARGB format
//

#ifndef GRAPHIC_UTILS_H
#define GRAPHIC_UTILS_H

#include "boot.h"


typedef union {
    struct {
        uint8_t b;
        uint8_t g;
        uint8_t r;
        uint8_t a;
    } ch;
    uint8_t     channel[4];
    uint32_t    value;
} pixel_t;

typedef struct {
    uint16_t	height;
    uint16_t	width;
    pixel_t*	pixels;
} pixmap_t;

typedef struct {
    uint32_t x;
    uint32_t y;
} position_t;

// Blends the given pixmap into the given background at the given position
// Uses the alpha channels to blend, and preserves the final alpha (so the
// resultant pixmap can be blended again with another background).
// ported from www.stereopsis.com/doubleblend.html
void blend( const pixmap_t *blendThis,            // Source image
            pixmap_t *blendInto,                  // Dest image
            const position_t position);         // Where to place the source image
// Returns the topleft co-ordinate where if you put the 'toCenter' pixmap,
// it is centered in the background.
position_t centeredIn( const pixmap_t *background, const pixmap_t *toCenter );

// Returns the topleft co-ordinate where if you put the given pixmap, its
// center will coincide with the th given center.
position_t centeredAt( const pixmap_t *pixmap, const position_t center );

// Utility function returns a position_t struct given the x and y coords as uint16
position_t pos(const uint16_t x, const uint16_t y);

// Flips the R and B components of all pixels in the given pixmap
void flipRB(pixmap_t *p);

// Utility function to get pixel at (x,y) in a pixmap
#define pixel(p,x,y) ((p)->pixels[(x) + (y) * (p)->width])

#endif//GRAPHIC_UTILS_H
