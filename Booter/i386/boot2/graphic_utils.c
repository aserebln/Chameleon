
 /* Graphic utility functions and data types
  * Prashant Vaibhav (C) 12/12/2008
  * Chameleon
  */
#include "boot.h"
#include "graphic_utils.h"
#include "gui.h"

void blend( const pixmap_t *blendThis,            // Source image
            pixmap_t *blendInto,                  // Dest image
            const position_t position)          // Where to place the source image
{
    uint16_t sx, sy, dx, dy;
    uint32_t dstrb, dstag, srcrb, srcag, drb, dag, rb, ag, alpha;
	
	uint16_t width = (blendThis->width + position.x < blendInto->width) ? blendThis->width: blendInto->width-position.x;
	uint16_t height = (blendThis->height + position.y < blendInto->height) ? blendThis->height: blendInto->height-position.y;
	
    for (dy = position.y, sy = 0; sy < height; dy++, sy++) {
        for (dx = position.x, sx = 0; sx < width; dx++, sx++) {
            alpha = (pixel(blendThis, sx, sy).ch.a);

	    /* Skip blending for fully transparent pixel */
	    if (alpha == 0) continue;

	    /* For fully opaque pixel, there is no need to interpolate */
	    if (alpha == 255) {
		    pixel(blendInto, dx, dy).value = pixel(blendThis, sx, sy).value;
		    continue;
	    }

	    /* For semi-transparent pixels, do a full blend */
	    //alpha++
		 /* This is needed to spread the alpha over [0..256] instead of [0..255]
			Boundary conditions were handled above */
            dstrb =  pixel(blendInto, dx, dy).value       & 0xFF00FF;
            dstag = (pixel(blendInto, dx, dy).value >> 8) & 0xFF00FF;
            srcrb =  pixel(blendThis, sx, sy).value       & 0xFF00FF;
            srcag = (pixel(blendThis, sx, sy).value >> 8) & 0xFF00FF;
            drb   = srcrb - dstrb;
            dag   = srcag - dstag;
            drb *= alpha; dag *= alpha;
            drb >>= 8; dag >>= 8;
            rb =  (drb + dstrb)       & 0x00FF00FF;
            ag = ((dag + dstag) << 8) & 0xFF00FF00;
            pixel(blendInto, dx, dy).value = (rb | ag);
        }
    }
}

position_t centeredIn( const pixmap_t *background, const pixmap_t *toCenter )
{
    position_t centered;
    centered.x = ( background->width  - toCenter->width  ) / 2;
    centered.y = ( background->height - toCenter->height ) / 2;
    return centered;
}

position_t centeredAt( const pixmap_t *pixmap, const position_t center )
{
    position_t topleft;
    topleft.x = center.x - (pixmap->width  / 2);
    topleft.y = center.y - (pixmap->height / 2);
    return topleft;
}

position_t pos(const uint16_t x, const uint16_t y) { position_t p; p.x = x; p.y = y; return p; }

void flipRB(pixmap_t *p)
{
	//if(testForQemu()) return;
	
	uint32_t x;
    register uint8_t tempB;
	for (x = 0; x < (p->height) * (p->width) ; x++) {
		tempB = (p->pixels[x]).ch.b;
        (p->pixels[x]).ch.b = (p->pixels[x]).ch.r;
        (p->pixels[x]).ch.r = tempB;
	}
}
