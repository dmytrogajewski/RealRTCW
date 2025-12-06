/*
===========================================================================

Return to Castle Wolfenstein single player GPL Source Code
Copyright (C) 1999-2010 id Software LLC, a ZeniMax Media company. 

This file is part of the Return to Castle Wolfenstein single player GPL Source Code ("RTCW SP Source Code").  

RTCW SP Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

RTCW SP Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with RTCW SP Source Code.  If not, see <http://www.gnu.org/licenses/>.

===========================================================================
*/

#include "ui_local.h"

/*
=================
UI_AddVisualEnhancementOptions

Adds visual enhancement menu items (HDR exposure, SSAO, etc.) to a menu
This can be called from menu scripts or programmatically
=================
*/
void UI_AddVisualEnhancementOptions( menuDef_t *menu, int startY ) {
	itemDef_t *item;
	editFieldDef_t *editDef;
	int y = startY;
	
	if ( !menu ) {
		return;
	}
	
	// HDR Exposure Slider
	if ( menu->itemCount < MAX_MENUITEMS ) {
		item = UI_Alloc( sizeof( itemDef_t ) );
		if ( item ) {
			memset( item, 0, sizeof( itemDef_t ) );
			Item_Init( item );
			
			item->type = ITEM_TYPE_SLIDER;
			item->text = "HDR Exposure";
			item->cvar = "r_hdrExposure";
			item->window.rect.x = 240;
			item->window.rect.y = y;
			item->window.rect.w = 360;
			item->window.rect.h = 20;
			item->textscale = 0.25f;
			item->textStyle = ITEM_TEXTSTYLE_SHADOWED;
			item->textalignment = ITEM_ALIGN_LEFT;
			item->window.flags = WINDOW_VISIBLE;
			item->parent = menu;
			
			editDef = UI_Alloc( sizeof( editFieldDef_t ) );
			if ( editDef ) {
				editDef->minVal = -2.0f;
				editDef->maxVal = 2.0f;
				editDef->defVal = 0.0f;
				item->typeData = editDef;
			}
			
			menu->items[menu->itemCount++] = item;
			y += 30;
		}
	}
	
	// HDR Gamma Slider
	if ( menu->itemCount < MAX_MENUITEMS ) {
		item = UI_Alloc( sizeof( itemDef_t ) );
		if ( item ) {
			memset( item, 0, sizeof( itemDef_t ) );
			Item_Init( item );
			
			item->type = ITEM_TYPE_SLIDER;
			item->text = "HDR Gamma";
			item->cvar = "r_hdrGamma";
			item->window.rect.x = 240;
			item->window.rect.y = y;
			item->window.rect.w = 360;
			item->window.rect.h = 20;
			item->textscale = 0.25f;
			item->textStyle = ITEM_TEXTSTYLE_SHADOWED;
			item->textalignment = ITEM_ALIGN_LEFT;
			item->window.flags = WINDOW_VISIBLE;
			item->parent = menu;
			
			editDef = UI_Alloc( sizeof( editFieldDef_t ) );
			if ( editDef ) {
				editDef->minVal = 1.5f;
				editDef->maxVal = 3.0f;
				editDef->defVal = 2.2f;
				item->typeData = editDef;
			}
			
			menu->items[menu->itemCount++] = item;
			y += 30;
		}
	}
	
	// SSAO Toggle (Yes/No)
	if ( menu->itemCount < MAX_MENUITEMS ) {
		item = UI_Alloc( sizeof( itemDef_t ) );
		if ( item ) {
			memset( item, 0, sizeof( itemDef_t ) );
			Item_Init( item );
			
			item->type = ITEM_TYPE_YESNO;
			item->text = "Screen-Space Ambient Occlusion";
			item->cvar = "r_ssao";
			item->window.rect.x = 240;
			item->window.rect.y = y;
			item->window.rect.w = 360;
			item->window.rect.h = 20;
			item->textscale = 0.25f;
			item->textStyle = ITEM_TEXTSTYLE_SHADOWED;
			item->textalignment = ITEM_ALIGN_LEFT;
			item->window.flags = WINDOW_VISIBLE;
			item->parent = menu;
			
			menu->items[menu->itemCount++] = item;
			y += 30;
		}
	}
	
	// SSAO Intensity Slider
	if ( menu->itemCount < MAX_MENUITEMS ) {
		item = UI_Alloc( sizeof( itemDef_t ) );
		if ( item ) {
			memset( item, 0, sizeof( itemDef_t ) );
			Item_Init( item );
			
			item->type = ITEM_TYPE_SLIDER;
			item->text = "SSAO Intensity";
			item->cvar = "r_ssaoIntensity";
			item->window.rect.x = 240;
			item->window.rect.y = y;
			item->window.rect.w = 360;
			item->window.rect.h = 20;
			item->textscale = 0.25f;
			item->textStyle = ITEM_TEXTSTYLE_SHADOWED;
			item->textalignment = ITEM_ALIGN_LEFT;
			item->window.flags = WINDOW_VISIBLE;
			item->parent = menu;
			
			editDef = UI_Alloc( sizeof( editFieldDef_t ) );
			if ( editDef ) {
				editDef->minVal = 0.5f;
				editDef->maxVal = 4.0f;
				editDef->defVal = 2.0f;
				item->typeData = editDef;
			}
			
			menu->items[menu->itemCount++] = item;
			y += 30;
		}
	}
	
	// Shadow Map Size (Multi-select)
	if ( menu->itemCount < MAX_MENUITEMS ) {
		item = UI_Alloc( sizeof( itemDef_t ) );
		if ( item ) {
			memset( item, 0, sizeof( itemDef_t ) );
			Item_Init( item );
			
			item->type = ITEM_TYPE_MULTI;
			item->text = "Shadow Map Size";
			item->cvar = "r_shadowMapSize";
			item->window.rect.x = 240;
			item->window.rect.y = y;
			item->window.rect.w = 360;
			item->window.rect.h = 20;
			item->textscale = 0.25f;
			item->textStyle = ITEM_TEXTSTYLE_SHADOWED;
			item->textalignment = ITEM_ALIGN_LEFT;
			item->window.flags = WINDOW_VISIBLE;
			item->parent = menu;
			
			multiDef_t *multiDef = UI_Alloc( sizeof( multiDef_t ) );
			if ( multiDef ) {
				memset( multiDef, 0, sizeof( multiDef_t ) );
				multiDef->strDef = qfalse; // Use numeric values
				multiDef->count = 5;
				multiDef->cvarList[0] = String_Alloc( "256" );
				multiDef->cvarList[1] = String_Alloc( "512" );
				multiDef->cvarList[2] = String_Alloc( "1024" );
				multiDef->cvarList[3] = String_Alloc( "2048" );
				multiDef->cvarList[4] = String_Alloc( "4096" );
				multiDef->cvarValue[0] = 256.0f;
				multiDef->cvarValue[1] = 512.0f;
				multiDef->cvarValue[2] = 1024.0f;
				multiDef->cvarValue[3] = 2048.0f;
				multiDef->cvarValue[4] = 4096.0f;
				item->typeData = multiDef;
			}
			
			menu->items[menu->itemCount++] = item;
		}
	}
}

