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

#include "tr_local.h"

#ifndef GL_RGBA16F
#define GL_RGBA16F 0x881A
#endif

#ifndef GL_RGBA32F
#define GL_RGBA32F 0x8814
#endif

#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#endif

#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0 0x8CE0
#endif

#ifndef GL_DEPTH_ATTACHMENT
#define GL_DEPTH_ATTACHMENT 0x8D00
#endif

#ifndef GL_FRAMEBUFFER_COMPLETE
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#endif

/*
=================
R_CheckHDRSupport
=================
*/
static qboolean R_CheckHDRSupport( void ) {
	if ( !qglGenFramebuffers || !qglBindFramebuffer || !qglFramebufferTexture2D ) {
		return qfalse;
	}
	
	// Check for floating point texture support
	// This is a basic check - in practice, we'd want to check for specific formats
	return qtrue;
}

/*
=================
R_InitHDRFramebuffers
=================
*/
qboolean R_InitHDRFramebuffers( void ) {
	GLenum status;
	GLint maxRenderbufferSize;
	
	if ( !r_hdr->integer ) {
		tr.hdrAvailable = qfalse;
		return qfalse;
	}
	
	if ( !R_CheckHDRSupport() ) {
		ri.Printf( PRINT_WARNING, "HDR framebuffers not available\n" );
		tr.hdrAvailable = qfalse;
		return qfalse;
	}
	
	// Get max renderbuffer size
	qglGetIntegerv( GL_MAX_RENDERBUFFER_SIZE, &maxRenderbufferSize );
	
	tr.hdrFramebuffer.width = glConfig.vidWidth;
	tr.hdrFramebuffer.height = glConfig.vidHeight;
	
	// Clamp to max size
	if ( tr.hdrFramebuffer.width > maxRenderbufferSize ) {
		tr.hdrFramebuffer.width = maxRenderbufferSize;
	}
	if ( tr.hdrFramebuffer.height > maxRenderbufferSize ) {
		tr.hdrFramebuffer.height = maxRenderbufferSize;
	}
	
	// Generate framebuffer
	qglGenFramebuffers( 1, &tr.hdrFramebuffer.fbo );
	qglBindFramebuffer( GL_FRAMEBUFFER, tr.hdrFramebuffer.fbo );
	
	// Create HDR color texture (16-bit float)
	qglGenTextures( 1, &tr.hdrFramebuffer.colorTexture );
	qglBindTexture( GL_TEXTURE_2D, tr.hdrFramebuffer.colorTexture );
	GLenum floatType = GL_FLOAT;
	// Use GL_FLOAT for now; can be upgraded to GL_HALF_FLOAT_ARB if extension available
	qglTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA16F, 
		tr.hdrFramebuffer.width, tr.hdrFramebuffer.height, 
		0, GL_RGBA, floatType, NULL );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	
	// Attach color texture
	qglFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 
		GL_TEXTURE_2D, tr.hdrFramebuffer.colorTexture, 0 );
	
	// Create depth texture
	qglGenTextures( 1, &tr.hdrFramebuffer.depthTexture );
	qglBindTexture( GL_TEXTURE_2D, tr.hdrFramebuffer.depthTexture );
	qglTexImage2D( GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, 
		tr.hdrFramebuffer.width, tr.hdrFramebuffer.height, 
		0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	
	// Attach depth texture
	qglFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, 
		GL_TEXTURE_2D, tr.hdrFramebuffer.depthTexture, 0 );
	
	// Optional: Create normal texture for deferred rendering
	qglGenTextures( 1, &tr.hdrFramebuffer.normalTexture );
	qglBindTexture( GL_TEXTURE_2D, tr.hdrFramebuffer.normalTexture );
	// Reuse floatType from above
	qglTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA16F, 
		tr.hdrFramebuffer.width, tr.hdrFramebuffer.height, 
		0, GL_RGBA, floatType, NULL );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	
	// Optional: Create position texture for deferred rendering
	qglGenTextures( 1, &tr.hdrFramebuffer.positionTexture );
	qglBindTexture( GL_TEXTURE_2D, tr.hdrFramebuffer.positionTexture );
	qglTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA16F, 
		tr.hdrFramebuffer.width, tr.hdrFramebuffer.height, 
		0, GL_RGBA, floatType, NULL );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	
	// Check framebuffer status
	status = qglCheckFramebufferStatus( GL_FRAMEBUFFER );
	if ( status != GL_FRAMEBUFFER_COMPLETE ) {
		ri.Printf( PRINT_WARNING, "HDR framebuffer incomplete: 0x%x\n", status );
		R_ShutdownHDRFramebuffers();
		tr.hdrAvailable = qfalse;
		return qfalse;
	}
	
	// Unbind framebuffer
	qglBindFramebuffer( GL_FRAMEBUFFER, 0 );
	qglBindTexture( GL_TEXTURE_2D, 0 );
	
	tr.hdrFramebuffer.initialized = qtrue;
	tr.hdrAvailable = qtrue;
	
	ri.Printf( PRINT_ALL, "HDR framebuffer initialized: %dx%d\n", 
		tr.hdrFramebuffer.width, tr.hdrFramebuffer.height );
	
	return qtrue;
}

/*
=================
R_ShutdownHDRFramebuffers
=================
*/
void R_ShutdownHDRFramebuffers( void ) {
	if ( !tr.hdrFramebuffer.initialized ) {
		return;
	}
	
	if ( tr.hdrFramebuffer.colorTexture ) {
		qglDeleteTextures( 1, &tr.hdrFramebuffer.colorTexture );
		tr.hdrFramebuffer.colorTexture = 0;
	}
	
	if ( tr.hdrFramebuffer.depthTexture ) {
		qglDeleteTextures( 1, &tr.hdrFramebuffer.depthTexture );
		tr.hdrFramebuffer.depthTexture = 0;
	}
	
	if ( tr.hdrFramebuffer.normalTexture ) {
		qglDeleteTextures( 1, &tr.hdrFramebuffer.normalTexture );
		tr.hdrFramebuffer.normalTexture = 0;
	}
	
	if ( tr.hdrFramebuffer.positionTexture ) {
		qglDeleteTextures( 1, &tr.hdrFramebuffer.positionTexture );
		tr.hdrFramebuffer.positionTexture = 0;
	}
	
	if ( tr.hdrFramebuffer.fbo ) {
		qglBindFramebuffer( GL_FRAMEBUFFER, 0 );
		qglDeleteFramebuffers( 1, &tr.hdrFramebuffer.fbo );
		tr.hdrFramebuffer.fbo = 0;
	}
	
	tr.hdrFramebuffer.initialized = qfalse;
	tr.hdrAvailable = qfalse;
}

/*
=================
R_BindHDRFramebuffer
=================
*/
void R_BindHDRFramebuffer( void ) {
	if ( !tr.hdrAvailable || !tr.hdrFramebuffer.initialized ) {
		return;
	}
	
	qglBindFramebuffer( GL_FRAMEBUFFER, tr.hdrFramebuffer.fbo );
	qglViewport( 0, 0, tr.hdrFramebuffer.width, tr.hdrFramebuffer.height );
}

/*
=================
R_UnbindHDRFramebuffer
=================
*/
void R_UnbindHDRFramebuffer( void ) {
	qglBindFramebuffer( GL_FRAMEBUFFER, 0 );
	qglViewport( 0, 0, glConfig.vidWidth, glConfig.vidHeight );
}

/*
=================
R_BlitHDRToScreen
=================
*/
void R_BlitHDRToScreen( void ) {
	if ( !tr.hdrAvailable || !tr.hdrFramebuffer.initialized ) {
		return;
	}
	
	// Use framebuffer blit if available
	if ( qglBlitFramebuffer ) {
		qglBindFramebuffer( GL_READ_FRAMEBUFFER, tr.hdrFramebuffer.fbo );
		qglBindFramebuffer( GL_DRAW_FRAMEBUFFER, 0 );
		qglBlitFramebuffer( 
			0, 0, tr.hdrFramebuffer.width, tr.hdrFramebuffer.height,
			0, 0, glConfig.vidWidth, glConfig.vidHeight,
			GL_COLOR_BUFFER_BIT, GL_LINEAR );
		qglBindFramebuffer( GL_READ_FRAMEBUFFER, 0 );
		qglBindFramebuffer( GL_DRAW_FRAMEBUFFER, 0 );
	} else {
		// Fallback: render fullscreen quad with tonemap shader
		// This will be implemented when we add the tonemap shader
		ri.Printf( PRINT_DEVELOPER, "R_BlitHDRToScreen: Blit not available, using fallback\n" );
	}
}

