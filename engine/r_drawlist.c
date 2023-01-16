// Emacs style mode select   -*- C -*-
//-----------------------------------------------------------------------------
//
// Copyright(C) 2007-2012 Samuel Villarreal
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
// 02111-1307, USA.
//
//-----------------------------------------------------------------------------
//
// DESCRIPTION: Vertex draw lists.
// Stores geometry info produced by R_RenderBSPNode into a list for optimal rendering
//
//-----------------------------------------------------------------------------

#include "doomdef.h"
#include "doomstat.h"
#include "d_devstat.h"
#include "r_main.h"
#include "r_things.h"
#include "r_lights.h"
#include "gl_texture.h"
#include "gl_main.h"
#include "r_drawlist.h"
#include "i_system.h"
#include "z_zone.h"
#include "gl_utils.h"

vtx_t drawVertex[MAXDLDRAWCOUNT];

static float envcolor[4] = { 0, 0, 0, 0 };

drawlist_t drawlist[NUMDRAWLISTS];

//
// DL_AddVertexList
//

vtxlist_t* DL_AddVertexList(drawlist_t* dl) {
	vtxlist_t* list;

	list = &dl->list[dl->index];

	if (list == &dl->list[dl->max - 1]) {
		// add a new list to the array
		dl->max++;

		// allocate array
		dl->list =
			(vtxlist_t*)Z_Realloc(dl->list,
				dl->max * sizeof(vtxlist_t), PU_LEVEL, NULL);

		memset(&dl->list[dl->max - 1], 0, sizeof(vtxlist_t));

		list = &dl->list[dl->index];
	}

	list->flags = 0;
	list->texid = 0;
	list->params = 0;

	return &dl->list[dl->index++];
}

//
// SortDrawList
//

static int SortDrawList(const void* a, const void* b) {
	vtxlist_t* xa = (vtxlist_t*)a;
	vtxlist_t* xb = (vtxlist_t*)b;

	return xb->texid - xa->texid;
}

//
// SortSprites
//

static int SortSprites(const vtxlist_t* a, const vtxlist_t* b) {
	visspritelist_t* xa = (visspritelist_t*)a->data;
	visspritelist_t* xb = (visspritelist_t*)b->data;

	return xb->dist - xa->dist;
}

//
// DL_ProcessDrawList
//

void DL_ProcessDrawList(int tag, boolean(*procfunc)(vtxlist_t*, int*)) {
	drawlist_t* dl;
	int i;
	int drawcount = 0;
	vtxlist_t* head;
	vtxlist_t* tail;
	boolean checkNightmare = false;

	if (tag < 0 && tag >= NUMDRAWLISTS) {
		return;
	}

	dl = &drawlist[tag];

	if (dl->max > 0) {
		int palette = 0;

		if (tag != DLT_SPRITE) {
			qsort(dl->list, dl->index, sizeof(vtxlist_t), SortDrawList);
		}
		else if (dl->index >= 2) {
			qsort(dl->list, dl->index, sizeof(vtxlist_t), SortSprites);
		}

		tail = &dl->list[dl->index];

		for (i = 0; i < dl->index; i++) {
			vtxlist_t* rover;

			head = &dl->list[i];

			// break if no data found in list
			if (!head->data) {
				break;
			}

			if (drawcount >= MAXDLDRAWCOUNT) {
				I_Error("DL_ProcessDrawList: Draw overflow by %i, tag=%i", dl->index, tag);
			}

			if (procfunc) {
				if (!procfunc(head, &drawcount)) {
					continue;
				}
			}

			rover = head + 1;

			if (tag != DLT_SPRITE) {
				if (rover != tail) {
					if (head->texid == rover->texid && head->params == rover->params) {
						continue;
					}
				}
			}

			// setup texture ID
			if (tag == DLT_SPRITE) {
				unsigned int flags = ((visspritelist_t*)head->data)->spr->flags;

				// textid in sprites contains hack that stores palette index data
				palette = head->texid >> 24;
				head->texid = head->texid & 0xffff;
				GL_BindSpriteTexture(head->texid, palette);

				// villsa 12152013 - change blend states for nightmare things
				if ((checkNightmare ^ (flags & MF_NIGHTMARE))) {
					if (!checkNightmare && (flags & MF_NIGHTMARE)) {
						glBlendFunc(GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR);
						checkNightmare ^= 1;
					}
					else if (checkNightmare && !(flags & MF_NIGHTMARE)) {
						glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
						checkNightmare ^= 1;
					}
				}
			}
			else {
				head->texid = (head->texid & 0xffff);
				GL_BindWorldTexture(head->texid, 0, 0);
			}

			// non sprite textures must repeat or mirrored-repeat
			if (tag == DLT_WALL) {
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
					head->flags & DLF_MIRRORS ? GL_MIRRORED_REPEAT : GL_REPEAT);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
					head->flags & DLF_MIRRORT ? GL_MIRRORED_REPEAT : GL_REPEAT);
			}

			int l = (head->params >> 1);

			GL_UpdateEnvTexture(D_RGBA(l, l, l, 0xff));
			glDrawGeometry(drawcount, drawVertex);

			// count vertex size
			if (devparm) {
				vertCount += drawcount;
			}

			drawcount = 0;
			head->data = NULL;
		}
	}
}

//
// DL_GetDrawListSize
//

int DL_GetDrawListSize(int tag) {
	int i;

	for (i = 0; i < NUMDRAWLISTS; i++) {
		drawlist_t* dl;

		if (i != tag) {
			continue;
		}

		dl = &drawlist[i];
		return dl->max * sizeof(vtxlist_t);
	}

	return 0;
}

//
// DL_BeginDrawList
//

void DL_BeginDrawList(boolean t) {
	glSetVertex(drawVertex);
	GL_SetTextureUnit(0, t);
}

//
// DL_Init
// Intialize draw lists
//

void DL_Init(void) {
	drawlist_t* dl;
	int i;

	for (i = 0; i < NUMDRAWLISTS; i++) {
		dl = &drawlist[i];

		dl->index = 0;
		dl->max = 1;
		dl->list = Z_Calloc(sizeof(vtxlist_t) * dl->max, PU_LEVEL, 0);
	}
}
