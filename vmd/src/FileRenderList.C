/***************************************************************************
 *cr                                                                       
 *cr            (C) Copyright 1995-2011 The Board of Trustees of the           
 *cr                        University of Illinois                       
 *cr                         All Rights Reserved                        
 *cr                                                                   
 ***************************************************************************/

/***************************************************************************
 * RCS INFORMATION:
 *
 *	$RCSfile: FileRenderList.C,v $
 *	$Author: johns $	$Locker:  $		$State: Exp $
 *	$Revision: 1.70 $	$Date: 2011/03/13 22:52:28 $
 *
 ***************************************************************************
 * DESCRIPTION:
 *
 * The FileRenderList class maintains a list of available FileRenderer
 * objects
 *
 ***************************************************************************/

#include "config.h"  // create dependency so new compile options cause rebuild
#include "FileRenderList.h"
#include "VMDApp.h"
#include "CmdRender.h"
#include "CommandQueue.h"
#include "Scene.h"
#include "DisplayDevice.h"
#include "TextEvent.h"
#include "Inform.h"
#include <stdlib.h>  // for system()

//
// Supported external rendering programs
//
#include "ArtDisplayDevice.h"         // Art ray tracer
#include "GelatoDisplayDevice.h"      // nVidia Gelato
#if defined(VMDLIBTACHYON) 
#include "LibTachyonDisplayDevice.h"  // Compiled-in Tachyon ray tracer
#endif
#if defined(VMDLIBGELATO)
#include "LibGelatoDisplayDevice.h"   // Compiled-in Gelato renderer
#endif
#include "MayaDisplayDevice.h"        // Autodesk Maya
#include "POV3DisplayDevice.h"        // POVRay 3.x 
#include "PSDisplayDevice.h"          // Postscript
#include "R3dDisplayDevice.h"         // Raster3D
#include "RadianceDisplayDevice.h"    // Radiance, unknown version 
#include "RayShadeDisplayDevice.h"    // Rayshade 4.0 
#include "RenderManDisplayDevice.h"   // RenderMan interface
#include "SnapshotDisplayDevice.h"    // Built-in snapshot capability
#include "STLDisplayDevice.h"         // Stereolithography files
#include "TachyonDisplayDevice.h"     // Tachyon ray tracer
#include "VrmlDisplayDevice.h"        // VRML 1.0 
#include "Vrml2DisplayDevice.h"       // VRML 2.0 / VRML97
#include "WavefrontDisplayDevice.h"   // Wavefront "OBJ" files
#include "X3DDisplayDevice.h"         // X3D (XML encoding)


// constructor, start off with the default means of rendering
FileRenderList::FileRenderList(VMDApp *vmdapp) : app(vmdapp) {
  add(new ArtDisplayDevice());
  add(new GelatoDisplayDevice());
#if defined(VMDLIBGELATO)
  // Only add the internally linked gelato display device to the
  // menu if the user has correctly set the GELATOHOME environment
  // variable.  If we allow them to use it otherwise, it may lead
  // to crashing, or failed renders.  This way they won't even see it
  // as an option unless they've got Gelato installed and the environment
  // at least minimally configured.
  if (getenv("GELATOHOME") != NULL) {
    add(new LibGelatoDisplayDevice());
  }
#endif
  // XXX until the native Maya ASCII export code is finished,
  // only show it when a magic environment variable is set.
  if (getenv("VMDENABLEMAYA") != NULL) {
    add(new MayaDisplayDevice());
  }
  add(new PSDisplayDevice());
  add(new R3dDisplayDevice());
  add(new RadianceDisplayDevice());
  add(new RayShadeDisplayDevice());
  add(new RenderManDisplayDevice());
  add(new SnapshotDisplayDevice(app->display));
  add(new STLDisplayDevice());
  add(new TachyonDisplayDevice());
#if defined(VMDLIBTACHYON)
  add(new LibTachyonDisplayDevice());
#endif
  add(new POV3DisplayDevice());
  add(new VrmlDisplayDevice());
  add(new Vrml2DisplayDevice());
  add(new WavefrontDisplayDevice());
  add(new X3DDisplayDevice());
  add(new X3DOMDisplayDevice());
}

// destructor, deallocate all the info
FileRenderList::~FileRenderList(void) {
  for (int i=0;i<renderList.num();i++)
    delete renderList.data(i);
}

// add a new render class with its corresponding name
void FileRenderList::add(FileRenderer *newRenderer) {
  if (newRenderer)
    renderList.add_name(newRenderer->name, newRenderer);
}

// figure out how many render classes are installed
int FileRenderList::num(void) {
  return renderList.num();
}

// return the name for the ith class, returns NULL if out of range
const char *FileRenderList::name(int i) {
  if (i>=0 && i < renderList.num()) {
    return renderList.name(i);
  }
  return NULL;
}

// return the "pretty" name (used in GUIs) for the ith class.
// returns NULL if out of range
const char *FileRenderList::pretty_name(int i) {
  if (i>=0 && i < renderList.num()) {
    const FileRenderer * fr = renderList.data(i);
    return fr->pretty_name();
  }
  return NULL;
}

// find class (case-insensitive) for a renderer name, else return NULL  
FileRenderer *FileRenderList::find(const char *rname) {
  int indx = renderList.typecode(rname);
  
  if (indx >= 0)
    return renderList.data(indx);
  else
    return NULL;
}

// given a "pretty" render name, return the corresponding class
FileRenderer *FileRenderList::find_pretty_name(const char *pretty) {
  int i;
  for (i=0; i<renderList.num(); i++) {
    if (!strcmp(pretty_name(i), pretty)) {
      return renderList.data(i); 
    }
  }
  return NULL;
}

// given a "pretty" renderer name, return the short name
const char *FileRenderList::find_short_name_from_pretty_name(const char *pretty) {
  const FileRenderer *fr = find_pretty_name(pretty);
  if (fr)
    return fr->visible_name();
  return NULL;
}

int FileRenderList::render(const char *filename, const char *method,
                           const char *extcmd) {
  msgInfo << "Rendering current scene to '" << filename << "' ..." << sendmsg;

  FileRenderer *render = find(method);
  if (!render) {
    msgErr << "Invalid render method '" << method << sendmsg;
    return FALSE;
  }

  // XXX Snapshot grabs the wrong buffer, so if we're doing snapshot, swap
  // the buffers, render, then swap back.
  if (!strcmp(method, "snapshot")) app->display->update(TRUE);
  int retval = app->scene->filedraw(render, filename, app->display);
  if (!strcmp(method, "snapshot")) app->display->update(TRUE);

  // if successful, execute external command
  if (retval && extcmd && *extcmd != '\0') {
    JString strbuf(extcmd);
    strbuf.gsub("%s", filename);
    // substitute display %w and %h for display width and height
    int w=100, h=100;
    char buf[32];
    app->display_get_size(&w, &h);
    sprintf(buf, "%d", w);
    strbuf.gsub("%w", buf);
    sprintf(buf, "%d", h);
    strbuf.gsub("%h", buf);
    msgInfo << "Executing post-render cmd '" << (const char *)strbuf << "' ..." << sendmsg;
    vmd_system(strbuf);
  }

  // return result
  msgInfo << "Rendering complete." << sendmsg;
  return retval;
}

int FileRenderList::set_render_option(const char *method, const char *option) {
  FileRenderer *ren;
  ren = find(method);
  if (!ren) {
    msgErr << "No rendering method '" << method << "' available." << sendmsg;
    return FALSE;
  }
  ren->set_exec_string(option);
  return TRUE;
} 

int FileRenderList::has_antialiasing(const char *method) {
  FileRenderer *ren = find(method);
  if (ren) return ren->has_antialiasing();
  return 0;
}

int FileRenderList::aasamples(const char *method, int aasamples) {
  FileRenderer *ren = find(method);
  if (ren) return ren->set_aasamples(aasamples);
  return -1;
}

int FileRenderList::aosamples(const char *method, int aosamples) {
  FileRenderer *ren = find(method);
  if (ren) return ren->set_aosamples(aosamples);
  return -1;
}

int FileRenderList::imagesize(const char *method, int *w, int *h) {
  FileRenderer *ren = find(method);
  if (!ren) return FALSE;
  return ren->set_imagesize(w, h);
}

int FileRenderList::has_imagesize(const char *method) {
  FileRenderer *ren = find(method);
  if (!ren) return FALSE;
  return ren->has_imagesize();
}

int FileRenderList::aspectratio(const char *method, float *aspect) {
  FileRenderer *ren = find(method);
  if (!ren) return FALSE;
  *aspect = ren->set_aspectratio(*aspect);
  return TRUE;
}

int FileRenderList::numformats(const char *method) {
  FileRenderer *ren = find(method);
  if (!ren) return 0;
  return ren->numformats();
}

const char *FileRenderList::format(const char *method, int i) {
  FileRenderer *ren = find(method);
  if (!ren) return NULL;
  if (i < 0) return ren->format();
  return ren->format(i);
}

int FileRenderList::set_format(const char *method, const char *format) {
  FileRenderer *ren = find(method);
  if (!ren) return FALSE;
  return ren->set_format(format);
}

