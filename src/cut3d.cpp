/* ----------------------------------------------------------------------
   SPARTA - Stochastic PArallel Rarefied-gas Time-accurate Analyzer
   http://sparta.sandia.gov
   Steve Plimpton, sjplimp@sandia.gov, Michael Gallis, magalli@sandia.gov
   Sandia National Laboratories

   Copyright (2012) Sandia Corporation.  Under the terms of Contract
   DE-AC04-94AL85000 with Sandia Corporation, the U.S. Government retains
   certain rights in this software.  This software is distributed under 
   the GNU General Public License.

   See the README file in the top-level SPARTA directory.
------------------------------------------------------------------------- */

#include "math.h"
#include "math_extra.h"
#include "cut3d.h"
#include "cut2d.h"
#include "surf.h"
#include "memory.h"
#include "error.h"

using namespace SPARTA_NS;

enum{UNKNOWN,OUTSIDE,INSIDE,OVERLAP};     // several files
enum{CTRI,CTRIFACE,FACEPGON,FACE};
enum{EXTERIOR,INTERIOR,BORDER};
enum{ENTRY,EXIT,TWO,CORNER};              // same as Cut2d

//#define VERBOSE
#define VERBOSE_ID 23506

/* ---------------------------------------------------------------------- */

Cut3d::Cut3d(SPARTA *sparta) : Pointers(sparta)
{
  cut2d = new Cut2d(sparta);
  memory->create(path1,12,3,"cut3d:path1");
  memory->create(path2,12,3,"cut3d:path2");

  // DEBUG
  //totcell = totsurf = totvert = totedge = 0;
}

/* ---------------------------------------------------------------------- */

Cut3d::~Cut3d()
{
  delete cut2d;
  memory->destroy(path1);
  memory->destroy(path2);

  // DEBUG
  //printf("TOTCELL %d\n",totcell);
  //printf("TOTSURF %d\n",totsurf);
  //printf("TOTVERT %d\n",totvert);
  //printf("TOTEDGE %d\n",totedge);
}

/* ----------------------------------------------------------------------
   compute intersections of surfs with grid cells
   for now, each proc computes for all cells
   done via 2 loops, one to count intersections, one to populate csurfs
   sets nsurf,csurfs for every grid cell with indices into global surf list
   also allocates csplits for every grid cell
------------------------------------------------------------------------- */

int Cut3d::surf2grid(cellint id_caller, double *lo_caller, double *hi_caller,
                     int *surfs_caller, int max)
{
  id = id_caller;
  lo = lo_caller;
  hi = hi_caller;
  surfs = surfs_caller;

  Surf::Point *pts = surf->pts;
  Surf::Tri *tris = surf->tris;
  int ntri = surf->ntri;

  double value;
  double *x1,*x2,*x3;

  nsurf = 0;
  for (int m = 0; m < ntri; m++) {
    x1 = pts[tris[m].p1].x;
    x2 = pts[tris[m].p2].x;
    x3 = pts[tris[m].p3].x;

    value = MAX(x1[0],x2[0]);
    if (MAX(value,x3[0]) < lo[0]) continue;
    value = MIN(x1[0],x2[0]);
    if (MIN(value,x3[0]) > hi[0]) continue;

    value = MAX(x1[1],x2[1]);
    if (MAX(value,x3[1]) < lo[1]) continue;
    value = MIN(x1[1],x2[1]);
    if (MIN(value,x3[1]) > hi[1]) continue;

    value = MAX(x1[2],x2[2]);
    if (MAX(value,x3[2]) < lo[2]) continue;
    value = MIN(x1[2],x2[2]);
    if (MIN(value,x3[2]) > hi[2]) continue;

    // 3 versions of this:
    // 1 = tri_hex_intersect with geometric line_tri_intersect,
    //     devel/cut3d.old1.py
    // 2 = tri_hex_intersect with tetvol line_tri_intersect, here
    // 3 = Sutherland-Hodgman clip algorithm, here and in devel/cut3d.py

    //if (tri_hex_intersect(x1,x2,x3,tris[m].norm)) {
    //  if (nsurf == max) return -1;
    //  surfs[nsurf++] = m;
    // }

    if (clip(x1,x2,x3)) {
      if (nsurf == max) return -1;
      surfs[nsurf++] = m;
    }
  }

  return nsurf;
}

/* ----------------------------------------------------------------------
   Sutherland-Hodgman clipping algorithm
   don't need to delete duplicate points since touching counts as intersection
------------------------------------------------------------------------- */

int Cut3d::clip(double *p0, double *p1, double *p2)
{
  int i,npath,nnew;
  double value;
  double *s,*e;
  double **path,**newpath;

  // intersect if any of tri vertices is within grid cell

  if (p0[0] >= lo[0] && p0[0] <= hi[0] &&
      p0[1] >= lo[1] && p0[1] <= hi[1] &&
      p0[2] >= lo[2] && p0[2] <= hi[2] &&
      p1[0] >= lo[0] && p1[0] <= hi[0] &&
      p1[1] >= lo[1] && p1[1] <= hi[1] &&
      p1[2] >= lo[2] && p1[2] <= hi[2] &&
      p2[0] >= lo[0] && p2[0] <= hi[0] &&
      p2[1] >= lo[1] && p2[1] <= hi[1] &&
      p2[2] >= lo[2] && p2[2] <= hi[2]) return 1;

  // initial path = tri vertices

  nnew = 3;
  memcpy(path1[0],p0,3*sizeof(double));
  memcpy(path1[1],p1,3*sizeof(double));
  memcpy(path1[2],p2,3*sizeof(double));

  // clip tri against each of 6 grid face planes

  for (int dim = 0; dim < 3; dim++) {
    path = path1;
    newpath = path2;
    npath = nnew;
    nnew = 0;

    value = lo[dim];
    s = path[npath-1];
    for (i = 0; i < npath; i++) {
      e = path[i];
      if (e[dim] >= value) {
        if (s[dim] < value) between(s,e,dim,value,newpath[nnew++]);
        memcpy(newpath[nnew++],e,3*sizeof(double));
      } else if (s[dim] >= value) between(e,s,dim,value,newpath[nnew++]);
      s = e;
    }
    if (!nnew) return 0;

    path = path2;
    newpath = path1;
    npath = nnew;
    nnew = 0;

    value = hi[dim];
    s = path[npath-1];
    for (i = 0; i < npath; i++) {
      e = path[i];
      if (e[dim] <= value) {
        if (s[dim] > value) between(s,e,dim,value,newpath[nnew++]);
        memcpy(newpath[nnew++],e,3*sizeof(double));
      } else if (s[dim] <= value) between(e,s,dim,value,newpath[nnew++]);
      s = e;
    }
    if (!nnew) return 0;
  }

  return nnew;
}

/* ----------------------------------------------------------------------
------------------------------------------------------------------------- */

int Cut3d::split(cellint id_caller, double *lo_caller, double *hi_caller, 
                 int nsurf_caller, int *surfs_caller,
                 double *&vols_caller, int *surfmap, 
                 int *corners, int &xsub, double *xsplit)
{
  id = id_caller;
  lo = lo_caller;
  hi = hi_caller;
  nsurf = nsurf_caller;
  surfs = surfs_caller;

  add_tris();

#ifdef VERBOSE
  if (id == VERBOSE_ID) print_bpg("BPG after added tris");
#endif

  int grazeflag = clip_tris();

  // DEBUG
  //totcell++;
  //totsurf += nsurf;
  //totvert += verts.n;
  //totedge += edges.n;

#ifdef VERBOSE
  if (id == VERBOSE_ID) print_bpg("BPG after clipped tris");
#endif

  if (empty) {
    vols.grow(1);
    vols[0] = 0.0;
    if (grazeflag) 
      corners[0] = corners[1] = corners[2] = corners[3] = 
        corners[4] = corners[5] = corners[6] = corners[7] = INSIDE;
    vols_caller = &vols[0];
    return 1;
  }

  ctri_volume();
  edge2face();

  double lo2d[2],hi2d[2];

  for (int iface = 0; iface < 6; iface++) {
    if (facelist[iface].n) {
      face_from_cell(iface,lo2d,hi2d);
      edge2clines(iface);
      cut2d->split_face(id,iface,lo2d,hi2d);
      add_face_pgons(iface);
    } else {
      face_from_cell(iface,lo2d,hi2d);
      add_face(iface,lo2d,hi2d);
    }
  }

  remove_faces();

#ifdef VERBOSE
  if (id == VERBOSE_ID) print_bpg("BPG after faces");
#endif

  check();

  walk();

#ifdef VERBOSE
  if (id == VERBOSE_ID) print_loops();
#endif

  loop2ph();

  int nsplit = phs.n;
  if (nsplit > 1) {
    create_surfmap(surfmap);
    xsub = split_point(surfmap,xsplit);
  }

  // set corners = OUTSIDE if corner pt is in list of edge points
  // else set corners = INSIDE

  int icorner;
  double *p1,*p2;

  corners[0] = corners[1] = corners[2] = corners[3] = 
    corners[4] = corners[5] = corners[6] = corners[7] = INSIDE;

  int nedge = edges.n;
  for (int iedge = 0; iedge < nedge; iedge++) {
    if (!edges[iedge].active) continue;
    p1 = edges[iedge].p1;
    p2 = edges[iedge].p2;
    icorner = corner(p1);
    if (icorner >= 0) corners[icorner] = OUTSIDE;
    icorner = corner(p2);
    if (icorner >= 0) corners[icorner] = OUTSIDE;
  }

  // store volumes in vector so can return ptr to it

  vols.grow(nsplit);
  for (int i = 0; i < nsplit; i++) vols[i] = phs[i].volume;
  vols_caller = &vols[0];

  return nsplit;
}

/* ----------------------------------------------------------------------
   add each triangle as vertex and edges to BPG
   add full edge even if outside cell, clipping comes later
------------------------------------------------------------------------- */

void Cut3d::add_tris()
{
  int i,m;
  int e1,e2,e3,dir1,dir2,dir3;
  double *p1,*p2,*p3;
  Surf::Tri *tri;
  Vertex *vert;
  Edge *edge;

  Surf::Point *pts = surf->pts;
  Surf::Tri *tris = surf->tris;

  verts.grow(nsurf);
  edges.grow(3*nsurf);
  verts.n = 0;
  edges.n = 0;

  int nvert = 0;
  for (i = 0; i < nsurf; i++) {
    m = surfs[i];
    tri = &tris[m];
    p1 = pts[tri->p1].x;
    p2 = pts[tri->p2].x;
    p3 = pts[tri->p3].x;

    vert = &verts[nvert];
    vert->active = 1;
    vert->style = CTRI;
    vert->label = i;
    vert->nedge = 0;
    vert->volume = 0.0;
    vert->norm = tri->norm;

    // look for each edge of tri
    // add to edges in forward dir if doesn't yet exist
    // add to edges in returned dir if already exists

    e1 = findedge(p1,p2,0,dir1);
    if (e1 < 0) {
      e1 = edges.n++;
      dir1 = 0;
      edge = &edges[e1];
      edge->style = CTRI;
      edge->nvert = 0;
      memcpy(edge->p1,p1,3*sizeof(double));
      memcpy(edge->p2,p2,3*sizeof(double));
    }
    edge_insert(e1,dir1,nvert,-1,-1,-1,-1);

    e2 = findedge(p2,p3,0,dir2);
    if (e2 < 0) {
      e2 = edges.n++;
      dir2 = 0;
      edge = &edges[e2];
      edge->style = CTRI;
      edge->nvert = 0;
      memcpy(edge->p1,p2,3*sizeof(double));
      memcpy(edge->p2,p3,3*sizeof(double));
    }
    edge_insert(e2,dir2,nvert,e1,dir1,-1,-1);

    e3 = findedge(p3,p1,0,dir3);
    if (e3 < 0) {
      e3 = edges.n++;
      dir3 = 0;
      edge = &edges[e3];
      edge->style = CTRI;
      edge->nvert = 0;
      memcpy(edge->p1,p3,3*sizeof(double));
      memcpy(edge->p2,p1,3*sizeof(double));
    }
    edge_insert(e3,dir3,nvert,e2,dir2,-1,-1);

    nvert++;
  }

  verts.n = nvert;
} 

/* ----------------------------------------------------------------------
------------------------------------------------------------------------- */

int Cut3d::clip_tris()
{
  int i,n,dim,lohi,ivert,iedge,jedge,idir,jdir,dirprev,nedge;
  int p1flag,p2flag;
  double value;
  double *p1,*p2;
  Edge *edge,*nextedge,*newedge;

  // loop over all 6 faces of cell
  
  int nvert = verts.n;

  for (int iface = 0; iface < 6; iface++) {
    dim = iface / 2;
    lohi = iface % 2;
    if (lohi == 0) value = lo[dim];
    else value = hi[dim];

    // mark all edges as unclipped
    // some may have been clipped and not cleared on previous face
      
    nedge = edges.n;
    for (iedge = 0; iedge < nedge; iedge++)
      if (edges[iedge].active) edges[iedge].clipped = 0;

    // loop over vertices, clip each of its edges to face
    // if edge already clipped, unset clip flag and keep edge as-is
  
    for (ivert = 0; ivert < nvert; ivert++) {
      iedge = verts[ivert].first;
      idir = verts[ivert].dirfirst;
      nedge = verts[ivert].nedge;

      for (i = 0; i < nedge; i++) {
        edge = &edges[iedge];

        if (edge->clipped) {
          edge->clipped = 0;
          iedge = edge->next[idir];
          idir = edge->dirnext[idir];
          continue;
        }

        // p1/p2 are pts in order of traversal

        if (idir == 0) {
          p1 = edge->p1;
          p2 = edge->p2;
        } else {
          p1 = edge->p2;
          p2 = edge->p1;
        }

        // p1/p2 flag = OUTSIDE/ON/INSIDE for edge pts

        if (lohi == 0) {
          if (p1[dim] < value) p1flag = OUTSIDE;
          else if (p1[dim] > value) p1flag = INSIDE;
          else p1flag = OVERLAP;
          if (p2[dim] < value) p2flag = OUTSIDE;
          else if (p2[dim] > value) p2flag = INSIDE;
          else p2flag = OVERLAP;
        } else {
          if (p1[dim] < value) p1flag = INSIDE;
          else if (p1[dim] > value) p1flag = OUTSIDE;
          else p1flag = OVERLAP;
          if (p2[dim] < value) p2flag = INSIDE;
          else if (p2[dim] > value) p2flag = OUTSIDE;
          else p2flag = OVERLAP;
        }

        // if both OUTSIDE or one OUTSIDE and other ON, delete edge
        // if both INSIDE or one INSIDE and other ON or both ON, keep as-is
        // if one INSIDE and one OUTSIDE, replace OUTSIDE pt with clip pt

#ifdef VERBOSE
        /*
        if (id == VERBOSE_ID && ivert == 1) {
          printf("EDGE %d %d: %d %d: %d %d: %d\n",iedge,idir,p1flag,p2flag,
                 edge->verts[0],edge->verts[1],edge->prev[0]);
        }
        */
#endif

        if (p1flag == OUTSIDE) {
          if (p2flag == OUTSIDE || p2flag == OVERLAP) edge_remove(edge,idir);
          else {
            if (idir == 0) between(p1,p2,dim,value,edge->p1);
            else between(p1,p2,dim,value,edge->p2);
            edge->clipped = 1;
          }
        } else if (p1flag == INSIDE) {
          if (p2flag == OUTSIDE) {
            if (idir == 0) between(p1,p2,dim,value,edge->p2);
            else between(p1,p2,dim,value,edge->p1);
            edge->clipped = 1;
          }
        } else {
          if (p2flag == OUTSIDE) edge_remove(edge,idir);
        }

        iedge = edge->next[idir];
        idir = edge->dirnext[idir];
      }

#ifdef VERBOSE
      /*
      if (id == VERBOSE_ID) {
        char str[24];
        sprintf(str,"Partial FACE %d %d\n",iface,ivert);
        print_bpg(str);
      }
      */
#endif

      // loop over edges in vertex again
      // iedge = this edge, jedge = next edge
      // p1 = last pt in iedge, pt = first pt in jedge
      // if p1 != p2, add edge between them
  
      edges.grow(edges.n + verts[ivert].nedge);
      iedge = verts[ivert].first;
      idir = verts[ivert].dirfirst;

      for (i = 0; i < verts[ivert].nedge; i++) {
        edge = &edges[iedge];
        jedge = edge->next[idir];
        jdir = edge->dirnext[idir];
        if (jedge < 0) {
          jedge = verts[ivert].first;
          jdir = verts[ivert].dirfirst;
        }

        if (idir == 0) p1 = edge->p2;
        else p1 = edge->p1;
        if (jdir == 0) p2 = edges[jedge].p1;
        else p2 = edges[jedge].p2;

        if (!samepoint(p1,p2)) {
          n = edges.n++;
          newedge = &edges[n];
          newedge->style = CTRI;
          newedge->nvert = 0;
          memcpy(newedge->p1,p1,3*sizeof(double));
          memcpy(newedge->p2,p2,3*sizeof(double));
          // convert jedge back to -1 for last vertex
          if (jedge == verts[ivert].first) jedge = -1;
          edge_insert(n,0,ivert,iedge,idir,jedge,jdir);
          i++;
        }

        iedge = jedge;
        idir = jdir;
      }
    }

#ifdef VERBOSE
    /*
    if (id == VERBOSE_ID) {
      char str[24];
      sprintf(str,"After FACE %d\n",iface);
      print_bpg(str);
    }
    */
#endif
  }

  // remove zero-length edges

  nedge = edges.n;

  for (iedge = 0; iedge < nedge; iedge++) {
    if (!edges[iedge].active) continue;
    edge = &edges[iedge];
    if (samepoint(edge->p1,edge->p2)) edge_remove(edge);
  }
  
  // remove vertices which have less than 3 edges
  // do this after deleting zero-length edges so vertices are updated
  // removals should have 2 or 0 edges, no verts should have 1 edge

  for (ivert = 0; ivert < nvert; ivert++)
    if (verts[ivert].nedge <= 2) vertex_remove(&verts[ivert]);

  // remove vertices which only graze the cell
  // grazing = all vertex pts in same face of cell and outward normal

  int grazeflag = 0;
  for (ivert = 0; ivert < nvert; ivert++) {
    if (!verts[ivert].active) continue;
    if (grazing(&verts[ivert])) {
      grazeflag = 1;
      vertex_remove(&verts[ivert]);
    }
  }

  // remove edges which now have no vertices

  for (iedge = 0; iedge < nedge; iedge++) {
    if (!edges[iedge].active) continue;
    if (edges[iedge].nvert == 0) edges[iedge].active = 0;
  }

  // set BPG empty flag if no active vertices

  empty = 1;
  for (ivert = 0; ivert < nvert; ivert++)
    if (verts[ivert].active) {
      empty = 0;
      break;
    }

  return grazeflag;
}

/* ----------------------------------------------------------------------
   compute volume of vertices
   when called, only clipped triangles exist
------------------------------------------------------------------------- */

void Cut3d::ctri_volume() 
{
  int i,iedge,idir,nedge;
  double zarea,volume;
  double *p0,*p1,*p2;
  Edge *edge;

  int nvert = verts.n;
  for (int ivert = 0; ivert < nvert; ivert++) {
    if (!verts[ivert].active) continue;
    iedge = verts[ivert].first;
    idir = verts[ivert].dirfirst;
    nedge = verts[ivert].nedge;
    
    if (idir == 0) p0 = edges[iedge].p1;
    else p0 = edges[iedge].p2;

    volume = 0.0;

    for (i = 0; i < nedge; i++) {
      edge = &edges[iedge];

      // compute projected volume of a convex polygon to zlo face
      // split polygon into triangles
      // each tri makes a tri-capped volume with zlo face
      // zarea = area of oriented tri projected into z plane
      // volume based on height of z midpt of tri above zlo face

      if (idir == 0) {
        p1 = edge->p1;
        p2 = edge->p2;
      } else {
        p1 = edge->p2;
        p2 = edge->p1;
      }
      zarea = 0.5 * ((p1[0]-p0[0])*(p2[1]-p0[1]) - (p1[1]-p0[1])*(p2[0]-p0[0]));
      volume -= zarea * ((p0[2]+p1[2]+p2[2])/3.0 - lo[2]);

      iedge = edge->next[idir];
      idir = edge->dirnext[idir];
    }

    verts[ivert].volume = volume;
  }
}

/* ----------------------------------------------------------------------
   assign all singlet edges to faces (0-5)
   singlet edge must be on one or two faces, two if on cell edge
   if along cell edge, assign to one of two faces based on
     dot product of inward face norm and norm of tri containing edge
------------------------------------------------------------------------- */

void Cut3d::edge2face()
{
  int n,iface,nface,ivert;
  int faces[6];
  double dot;
  double norm_inward[3];
  double *trinorm;
  Edge *edge;

  // insure each facelist has sufficient length

  int nedge = edges.n;
  for (int i = 0; i < 6; i++) {
    facelist[i].grow(nedge);
    facelist[i].n = 0;
  }

  // loop over edges, assign singlets to exactly one face

  for (int iedge = 0; iedge < nedge; iedge++) {
    if (!edges[iedge].active) continue;
    if (edges[iedge].nvert == 3) continue;
    edge = &edges[iedge];

    nface = which_faces(edge->p1,edge->p2,faces);
    if (nface == 0) {
      printf("CELL ID %d\n",id);
      error->one(FLERR,"Singlet BPG edge not on cell face");
    }
    else if (nface == 1) iface = faces[0];
    else if (nface == 2) {
      iface = faces[0];
      norm_inward[0] = norm_inward[1] = norm_inward[2] = 0.0;
      if (iface % 2) norm_inward[iface/2] = -1.0;
      norm_inward[iface/2] = 1.0;
      if (edge->nvert == 1) ivert = edge->verts[0];
      else ivert = edge->verts[1];
      trinorm = verts[ivert].norm;
      dot = norm_inward[0]*trinorm[0] + norm_inward[1]*trinorm[1] + 
        norm_inward[2]*trinorm[2];
      if (dot > 0.0) iface = faces[1];
    } else {
      printf("CELL ID %d\n",id);
      error->one(FLERR,"BPG edge on more than 2 faces");
    }

    n = facelist[iface].n;
    facelist[iface][n++] = iedge;
    facelist[iface].n = n;
  }
}

/* ----------------------------------------------------------------------
   build a 2d CLINES data structure
   from all singlet edges assigned to iface (0-5)
   order pts in edge for tri traversing edge in forward order
   flip edge if in a flip face = faces 0,3,4
   edge label in clines = edge index in BPG
------------------------------------------------------------------------- */

void Cut3d::edge2clines(int iface)
{      
  int iedge;
  double *p1,*p2;
  Edge *edge;
  Cut2d::Cline *cline;

  MyVec<Cut2d::Cline> *clines = &cut2d->clines;

  int flip = 0;
  if (iface == 0 || iface == 3 || iface == 4) flip = 1;

  int nline = facelist[iface].n;
  clines->n = 0;
  clines->grow(nline);

  for (int i = 0; i < nline; i++) {
    iedge = facelist[iface][i];
    edge = &edges[iedge];
    if (edge->nvert == 1) {
      p1 = edge->p1;
      p2 = edge->p2;
    } else {
      p1 = edge->p2;
      p2 = edge->p1;
    }
    cline = &(*clines)[i];
    cline->line = iedge;
    if (flip) {
      compress2d(iface,p1,cline->y);
      compress2d(iface,p2,cline->x);
    } else {
      compress2d(iface,p1,cline->x);
      compress2d(iface,p2,cline->y);
    }
  }

  clines->n = nline;
}

/* ----------------------------------------------------------------------
   add one or more face polygons as vertices to BPG
   have to convert pts computed by cut2d back into 3d pts on face
------------------------------------------------------------------------- */

void Cut3d::add_face_pgons(int iface)
{
  int iloop,mloop,nloop,ipt,mpt,npt;
  int iedge,dir,prev,dirprev;
  double p1[3],p2[3];
  Vertex *vert;
  Edge *edge;
  Cut2d::PG *pg;
  Cut2d::Loop *loop;
  Cut2d::Point *p12d,*p22d;

  MyVec<Cut2d::PG> *pgs = &cut2d->pgs;
  MyVec<Cut2d::Loop> *loops = &cut2d->loops;
  MyVec<Cut2d::Point> *points = &cut2d->points;

  int flip = 0;
  if (iface == 0 || iface == 3 || iface == 4) flip = 1;

  double value;
  int dim = iface / 2;
  int lohi = iface % 2;
  if (lohi == 0) value = lo[dim];
  else value = hi[dim];

  int npg = pgs->n;
  int nvert = verts.n;
  verts.grow(nvert+npg);

  for (int ipg = 0; ipg < npg; ipg++) {
    pg = &(*pgs)[ipg];

    vert = &verts[nvert];
    vert->active = 1;
    vert->style = FACEPGON;
    vert->label = iface;
    if (iface == 5) vert->volume = pg->area * (hi[2]-lo[2]);
    else vert->volume = 0.0;
    vert->nedge = 0;
    vert->norm = NULL;

    prev = -1;
    dirprev = -1;

    nloop = pg->n;
    mloop = pg->first;
    for (iloop = 0; iloop < nloop; iloop++) {
      loop = &(*loops)[mloop];
      npt = loop->n;
      mpt = loop->first;
      edges.grow(edges.n + npt);

      for (ipt = 0; ipt < npt; ipt++) {
        p12d = &(*points)[mpt];
        mpt = p12d->next;
        p22d = &(*points)[mpt];
        expand2d(iface,value,p12d->x,p1);
        expand2d(iface,value,p22d->x,p2);

        // edge was from a CTRI vertex
        // match in opposite order that CTRI vertex matched it

        if (p12d->type == ENTRY || p12d->type == TWO) {
          iedge = p12d->line;
          edge = &edges[iedge];
          edge->style = CTRIFACE;
          if (edge->nvert == 1) dir = 1;
          else dir = 0;
          edge_insert(iedge,dir,nvert,prev,dirprev,-1,-1);
          prev = iedge;
          dirprev = dir;
          continue;
        }

        // face edge not from a CTRI
        // unflip edge if in a flip face

        if (flip) iedge = findedge(p2,p1,0,dir);
        else iedge = findedge(p1,p2,0,dir);

        if (iedge >= 0) {
          edge_insert(iedge,dir,nvert,prev,dirprev,-1,-1);
          prev = iedge;
          dirprev = 1;
          continue;
        }

        iedge = edges.n++;
        edge = &edges[iedge];
        edge->style = FACEPGON;
        edge->nvert = 0;
        if (flip) {
          memcpy(edge->p1,p2,3*sizeof(double));
          memcpy(edge->p2,p1,3*sizeof(double));
        } else {
          memcpy(edge->p1,p1,3*sizeof(double));
          memcpy(edge->p2,p2,3*sizeof(double));
        }
        dir = 0;
        edge_insert(iedge,dir,nvert,prev,dirprev,-1,-1);
        prev = iedge;
        dirprev = 0;
      }
      mloop = loop->next;
    }

    nvert++;
  }

  verts.n = nvert;
}

/* ----------------------------------------------------------------------
   add an entire cell face as vertex to BPG
   if outerflag2d = 0, create new vertex
   else face polygon already exists, so add edges to it
   caller sets outerflag2d if cut2d requires adding perimeter face edges
------------------------------------------------------------------------- */

void Cut3d::add_face(int iface, double *lo2d, double *hi2d) 
{
  int i,j,iedge,dir,prev,dirprev;
  double p1[3],p2[3];
  Vertex *vert;
  Edge *edge;

  int nvert = verts.n++;
  verts.grow(nvert + 1);
  vert = &verts[nvert];
  vert->active = 1;
  vert->style = FACE;
  vert->label = iface;
  if (iface == 5)
    vert->volume = (hi[0]-lo[0]) * (hi[1]-lo[1]) * (hi[2]-lo[2]);
  else vert->volume = 0.0;
  vert->nedge = 0;
  vert->norm = NULL;

  double value;
  int dim = iface / 2;
  int lohi = iface % 2;
  if (lohi == 0) value = lo[dim];
  else value = hi[dim];

  // usual ordering of points in face as LL,LR,UR,UL
  // flip order if in a flip face

  int flip = 0;
  if (iface == 0 || iface == 3 || iface == 4) flip = 1;

  double cpts[4][2];

  if (flip) {
    cpts[0][0] = lo2d[0]; cpts[0][1] = lo2d[1];
    cpts[1][0] = lo2d[0]; cpts[1][1] = hi2d[1];
    cpts[2][0] = hi2d[0]; cpts[2][1] = hi2d[1];
    cpts[3][0] = hi2d[0]; cpts[3][1] = lo2d[1];
  } else {
    cpts[0][0] = lo2d[0]; cpts[0][1] = lo2d[1];
    cpts[1][0] = hi2d[0]; cpts[1][1] = lo2d[1];
    cpts[2][0] = hi2d[0]; cpts[2][1] = hi2d[1];
    cpts[3][0] = lo2d[0]; cpts[3][1] = hi2d[1];
  }

  if (vert->nedge) {
    prev = vert->last;
    dirprev = vert->dirlast;
  } else {
    prev = -1;
    dirprev = -1;
  }

  edges.grow(edges.n + 4);

  for (i = 0; i < 4; i++) {
    j = i+1;
    if (j == 4) j = 0;
    expand2d(iface,value,&cpts[i][0],p1);
    expand2d(iface,value,&cpts[j][0],p2);
    iedge = findedge(p1,p2,1,dir);

    if (iedge >= 0) {
      edge_insert(iedge,dir,nvert,prev,dirprev,-1,-1);
      prev = iedge;
      dirprev = 1;
      continue;
    }

    iedge = edges.n++;
    edge = &edges[iedge];
    edge->style = vert->style;
    edge->nvert = 0;
    memcpy(edge->p1,p1,3*sizeof(double));
    memcpy(edge->p2,p2,3*sizeof(double));
    dir = 0;
    edge_insert(iedge,dir,nvert,prev,dirprev,-1,-1);
    prev = iedge;
    dirprev = 0;
  }
}

/* ----------------------------------------------------------------------
   remove any FACE vertices with one or more unconnected edges
   unconnected means the edge is only part of this vertex
   mark vertex as inactive, decrement their edges,
     also set edges inactive if they are no longer attached to vertices
   iterate twice since another face may become unconnected 
------------------------------------------------------------------------- */

void Cut3d::remove_faces()
{
  int i,ivert,iedge,dir;
  Vertex *vert;
  Edge *edge;

  int nvert = verts.n;

  for (int iter = 0; iter < 2; iter++)
    for (ivert = 0; ivert < nvert; ivert++) {
      if (!verts[ivert].active) continue;
      if (verts[ivert].style != FACE) continue;
      vert = &verts[ivert];

      iedge = vert->first;
      dir = vert->dirfirst;
      for (i = 0; i < 4; i++) {
        edge = &edges[iedge];
        if (edge->nvert == 1 || edge->nvert == 2) break;
        iedge = edge->next[dir];
        dir = edge->dirnext[dir];
      }
      if (i < 4) vertex_remove(vert);
    }
}

/* ----------------------------------------------------------------------
   check BPG for consistency
   vertices have 3 or more unique edges that point back to it
   edges have 2 unique vertices
------------------------------------------------------------------------- */

void Cut3d::check()
{
  int i,iedge,dir,nedge,last,dirlast;
  Vertex *vert;
  Edge *edge;

  // mark all edges as unclipped
  // use for detecting duplicate edges in same vertex
      
  nedge = edges.n;
  for (iedge = 0; iedge < nedge; iedge++)
    if (edges[iedge].active) edges[iedge].clipped = 0;

  // check vertices
  // for each vertex: mark edges as see them, unmark all edges at end

  int nvert = verts.n;
  for (int ivert = 0; ivert < nvert; ivert++) {
    if (!verts[ivert].active) continue;
    vert = &verts[ivert];
    if (vert->nedge < 3) {
      printf("CELL ID %d\n",id);
      error->one(FLERR,"Vertex has less than 3 edges");
    }

    nedge = vert->nedge;
    iedge = vert->first;
    dir = vert->dirfirst;

    for (i = 0; i < nedge; i++) {
      edge = &edges[iedge];
      if (!edge->active) {
        printf("CELL ID %d\n",id);
        error->one(FLERR,"Vertex contains invalid edge");
      }
      if (edge->verts[dir] != ivert) {
        printf("CELL ID %d %d %d\n",id,ivert,iedge);
        error->one(FLERR,"Vertex contains edge that doesn't point to it");
      }
      if (edge->clipped) {
        printf("CELL ID %d\n",id);
        error->one(FLERR,"Vertex contains duplicate edge");
      }
      edge->clipped = 1;
      last = iedge;
      dirlast = dir;
      iedge = edge->next[dir];
      dir = edge->dirnext[dir];
    }

    if (last != vert->last || dirlast != vert->dirlast) {
      printf("CELL ID %d\n",id);
      error->one(FLERR,"Vertex pointers to last edge are invalid");
    }

    iedge = vert->first;
    dir = vert->dirfirst;
    for (i = 0; i < nedge; i++) {
      edge = &edges[iedge];
      edge->clipped = 0;
      iedge = edge->next[dir];
      dir = edge->dirnext[dir];
    }
  }

  // check edges

  nedge = edges.n;
  for (int iedge = 0; iedge < nedge; iedge++) {
    if (!edges[iedge].active) continue;
    edge = &edges[iedge];
    if (edge->nvert != 3) {
      printf("CELL ID %d %d\n",id,iedge);
      error->one(FLERR,"Edge not part of 2 vertices");
    }
    if (edge->verts[0] == edge->verts[1]) {
      printf("CELL ID %d\n",id);
      error->one(FLERR,"Edge part of same vertex twice");
    }
    if (edge->verts[0] >= nvert || !verts[edge->verts[0]].active) {
      printf("CELL ID %d\n",id);
      error->one(FLERR,"Edge part of invalid vertex");
    }
    if (edge->verts[1] >= nvert || !verts[edge->verts[1]].active) {
      printf("CELL ID %d\n",id);
      error->one(FLERR,"Edge part of invalid vertex");
    }
  }
}

/* ----------------------------------------------------------------------
   convert BPG into simple closed polyhedra, not nested
   walk BPG from any unused vertex, flagging vertices as used
   stack is list of new vertices to process
   loop over edges of pgon, add its unused neighbors to stack
   when stack is empty, loop is closed
   accumulate volume of polyhedra as walk it from volume of each vertex
------------------------------------------------------------------------- */

void Cut3d::walk()
{
  int i,flag,ncount,ivert,firstvert,iedge,dir,nedge,prev;
  double volume;
  Vertex *vert;
  Edge *edge;

  // used = 0/1 flag for whether a vertex is already part of a loop
  // only active vertices are eligible

  int nvert = verts.n;
  used.grow(nvert);
  for (int ivert = 0; ivert < nvert; ivert++) {
    if (verts[ivert].active) used[ivert] = 0;
    else used[ivert] = 1;
  }
  used.n = nvert;

  // stack = list of vertex indices to process
  // max size = # of vertices

  stack.grow(nvert);
  int nstack = 0;

  // iterate over all vertices
  // start a loop at any unused vertex
  // add more vertices to loop via stack
  // check all neighbor vertices via shared edges
  // if neighbor vertex is unused, add to stack
  // stop when stack is empty

  int nloop = 0;

  for (int i = 0; i < nvert; i++) {
    if (used[i]) continue;
    volume = 0.0;
    flag = INTERIOR;
    ncount = 0;

    stack[0] = firstvert = i;
    nstack = 1;
    used[i] = 1;
    prev = -1;

    while (nstack) {
      nstack--;
      ivert = stack[nstack];
      ncount++;

      vert = &verts[ivert];
      if (vert->style != CTRI) flag = BORDER;
      volume += vert->volume;

      nedge = vert->nedge;
      iedge = vert->first;
      dir = vert->dirfirst;

      for (i = 0; i < nedge; i++) {
        edge = &edges[iedge];
        if (!used[edge->verts[0]]) {
          stack[nstack++] = edge->verts[0];
          used[edge->verts[0]] = 1;
        }
        if (!used[edge->verts[1]]) {
          stack[nstack++] = edge->verts[1];
          used[edge->verts[1]] = 1;
        }
        iedge = edge->next[dir];
        dir = edge->dirnext[dir];
      }

      if (prev >= 0) verts[prev].next = ivert;
      prev = ivert;
    }
    vert->next = -1;

    loops.grow(nloop+1);
    loops[nloop].volume = volume;
    loops[nloop].flag = flag;
    loops[nloop].n = ncount;
    loops[nloop].first = firstvert;
    nloop++;
  }

  loops.n = nloop;
}

/* ----------------------------------------------------------------------
------------------------------------------------------------------------- */

void Cut3d::loop2ph()
{
  int positive = 0;
  int negative = 0;

  int nloop = loops.n;
  for (int i = 0; i < nloop; i++)
    if (loops[i].volume > 0.0) positive++;
    else negative++;
  if (positive == 0) error->one(FLERR,"No positive volumes in cell");
  if (positive > 1 && negative) 
    error->one(FLERR,"More than one positive volume with a negative volume");

  phs.grow(positive);

  if (positive == 1) {
    double volume = 0.0;
    for (int i = 0; i < nloop; i++) {
      volume += loops[i].volume;
      loops[i].next = i+1;
    }
    loops[nloop-1].next = -1;

    if (volume < 0.0) 
      error->one(FLERR,"Single volume is negative, inverse donut");

    phs[0].volume = volume;
    phs[0].n = nloop;
    phs[0].first = 0;

  } else {
    for (int i = 0; i < nloop; i++) {
      phs[i].volume = loops[i].volume;
      phs[i].n = 1;
      phs[i].first = i;
      loops[i].next = -1;
    }
  }

  phs.n = positive;
}

/* ----------------------------------------------------------------------
   assign each tri index in list to one of the split cells in PH
   return surfmap[i] = which PH the Ith tri index is assigned to
   set surfmap[i] = -1 if the tri did not end up in a PH
     could have been discarded in clip_tris()
     due to touching cell or lying along a cell edge or face
------------------------------------------------------------------------- */

void Cut3d::create_surfmap(int *surfmap)
{
  for (int i = 0; i < nsurf; i++) surfmap[i] = -1;

  int iloop,nloop,mloop,ivert,nvert,mvert;

  int nph = phs.n;
  for (int iph = 0; iph < nph; iph++) {
    nloop = phs[iph].n;
    mloop = phs[iph].first;
    for (iloop = 0; iloop < nloop; iloop++) {
      nvert = loops[mloop].n;
      mvert = loops[mloop].first;
      for (ivert = 0; ivert < nvert; ivert++) {
        if (verts[mvert].style == CTRI || verts[mvert].style == CTRIFACE)
          surfmap[verts[mvert].label] = iph;
        mvert = verts[mvert].next;
      }
      mloop = loops[mloop].next;
    }
  }
}

/* ----------------------------------------------------------------------
------------------------------------------------------------------------- */

int Cut3d::split_point(int *surfmap, double *xsplit)
{
  int itri;
  double *x1,*x2,*x3;
  double a[3],b[3],c[3];

  Surf::Point *pts = surf->pts;
  Surf::Tri *tris = surf->tris;

  // if end pt of any line with non-negative surfmap is in/on cell, return

  for (int i = 0; i < nsurf; i++) {
    if (surfmap[i] < 0) continue;
    itri = surfs[i];
    x1 = pts[tris[itri].p1].x;
    x2 = pts[tris[itri].p2].x;
    x3 = pts[tris[itri].p3].x;
    if (ptflag(x1) != EXTERIOR) {
      xsplit[0] = x1[0]; xsplit[1] = x1[1]; xsplit[2] = x1[2];
      return surfmap[i];
    }
    if (ptflag(x2) != EXTERIOR) {
      xsplit[0] = x2[0]; xsplit[1] = x2[1]; xsplit[2] = x2[2];
      return surfmap[i];
    }
    if (ptflag(x3) != EXTERIOR) {
      xsplit[0] = x3[0]; xsplit[1] = x3[1]; xsplit[2] = x3[2];
      return surfmap[i];
    }
  }

  // clip 1st line with non-negative surfmap to cell, and return clip point

  for (int i = 0; i < nsurf; i++) {
    if (surfmap[i] < 0) continue;
    itri = surfs[i];
    x1 = pts[tris[itri].p1].x;
    x2 = pts[tris[itri].p2].x;
    x3 = pts[tris[itri].p3].x;
    clip(x1,x2,x3);
    xsplit[0] = path1[0][0]; xsplit[1] = path1[0][1]; xsplit[2] = path1[0][2];
    return surfmap[i];
  }

  error->one(FLERR,"Could not find split point in split cell");
  return -1;
}


/* ----------------------------------------------------------------------
   insert edge IEDGE in DIR for ivert
   also update vertex info for added edge
------------------------------------------------------------------------- */

void Cut3d::edge_insert(int iedge, int dir, int ivert, 
                        int iprev, int dirprev, int inext, int dirnext)
{
  Edge *edge = &edges[iedge];

  if (dir == 0) {
    edge->nvert += 1;
    edge->verts[0] = ivert;
  } else {
    edge->nvert += 2;
    edge->verts[1] = ivert;
  }

  edge->active = 1;
  edge->clipped = 0;

  // set prev/next pointers for doubly linked list of edges

  edge->next[dir] = inext;
  edge->prev[dir] = iprev;

  if (inext >= 0) {
    edge->dirnext[dir] = dirnext;
    Edge *next = &edges[inext];
    next->prev[dirnext] = iedge;
    next->dirprev[dirnext] = dir;
  } else edge->dirnext[dir] = -1;

  if (iprev >= 0) {
    edge->dirprev[dir] = dirprev;
    Edge *prev = &edges[iprev];
    prev->next[dirprev] = iedge;
    prev->dirnext[dirprev] = dir;
  } else edge->dirprev[dir] = -1;

  // add edge info to owning vertex

  verts[ivert].nedge++;
  if (iprev < 0) {
    verts[ivert].first = iedge;
    verts[ivert].dirfirst = dir;
  }
  if (inext < 0) {
    verts[ivert].last = iedge;
    verts[ivert].dirlast = dir;
  }
}

/* ----------------------------------------------------------------------
   complete edge removal in both dirs
   will leave edge marked inactive
------------------------------------------------------------------------- */

void Cut3d::edge_remove(Edge *edge) 
{
  if (edge->verts[0] >= 0) edge_remove(edge,0);
  if (edge->verts[1] >= 0) edge_remove(edge,1);
}

/* ----------------------------------------------------------------------
   edge removal in DIR
   also update vertex info for removed edge
   mark edge inactive if its nvert -> 0
------------------------------------------------------------------------- */

void Cut3d::edge_remove(Edge *edge, int dir) 
{
  int ivert = edge->verts[dir];
  edge->verts[dir] = -1;
  if (dir == 0) edge->nvert--;
  else edge->nvert -= 2;
  if (edge->nvert == 0) edge->active = 0;

  // reset prev/next pointers for doubly linked list to skip this edge

  if (edge->prev[dir] >= 0) {
    Edge *prev = &edges[edge->prev[dir]];
    int dirprev = edge->dirprev[dir];
    prev->next[dirprev] = edge->next[dir];
    prev->dirnext[dirprev] = edge->dirnext[dir];
  }

  if (edge->next[dir] >= 0) {
    Edge *next = &edges[edge->next[dir]];
    int dirnext = edge->dirnext[dir];
    next->prev[dirnext] = edge->prev[dir];
    next->dirprev[dirnext] = edge->dirprev[dir];
  }

  // update vertex for removal of this edge

  verts[ivert].nedge--;
  if (edge->prev[dir] < 0) {
    verts[ivert].first = edge->next[dir];
    verts[ivert].dirfirst = edge->dirnext[dir];
  }
  if (edge->next[dir] < 0) {
    verts[ivert].last = edge->prev[dir];
    verts[ivert].dirlast = edge->dirprev[dir];
  }
}

/* ----------------------------------------------------------------------
   remove a vertex and all edges it includes
------------------------------------------------------------------------- */

void Cut3d::vertex_remove(Vertex *vert) 
{
  Edge *edge;

  vert->active = 0;

  int iedge = vert->first;
  int dir = vert->dirfirst;
  int nedge = vert->nedge;

  for (int i = 0; i < nedge; i++) {
    edge = &edges[iedge];
    if (dir == 0) edge->nvert--;
    else edge->nvert -= 2;
    if (edge->nvert == 0) edge->active = 0;
    edge->verts[dir] = -1;
    iedge = edge->next[dir];
    dir = edge->dirnext[dir];
  }
}

/* ----------------------------------------------------------------------
   a planar polygon is grazing if it lies entirely in plane of any face of cell
   and its normal is outward with respect to cell
   return 1 if grazing else 0
------------------------------------------------------------------------- */

int Cut3d::grazing(Vertex *vert) 
{
  int count[6];
  double *p;
  Edge *edge;

  int iedge = vert->first;
  int idir = vert->dirfirst;
  int nedge = vert->nedge;

  count[0] = count[1] = count[2] = count[3] = count[4] = count[5] = 0;

  for (int i = 0; i < nedge; i++) {
    edge = &edges[iedge];
    if (idir == 0) p = edge->p1;
    else p = edge->p2;

    if (p[0] == lo[0]) count[0]++;
    if (p[0] == hi[0]) count[1]++;
    if (p[1] == lo[1]) count[2]++;
    if (p[1] == hi[1]) count[3]++;
    if (p[2] == lo[2]) count[4]++;
    if (p[2] == hi[2]) count[5]++;

    iedge = edge->next[idir];
    idir = edge->dirnext[idir];
  }

  double *norm = vert->norm;
  if (count[0] == nedge && norm[0] < 0.0) return 1;
  if (count[1] == nedge && norm[0] > 0.0) return 1;
  if (count[2] == nedge && norm[1] < 0.0) return 1;
  if (count[3] == nedge && norm[1] > 0.0) return 1;
  if (count[4] == nedge && norm[2] < 0.0) return 1;
  if (count[5] == nedge && norm[2] > 0.0) return 1;
  return 0;
}

/* ----------------------------------------------------------------------
   identify which cell faces edge between p1,p2 is on
   p1,p2 assumed to be on surface or interior of cell
   return list of face IDs (0-5)
   list length can be 0,1,2
------------------------------------------------------------------------- */

int Cut3d::which_faces(double *p1, double *p2, int *faces)
{
  int n = 0;
  if (p1[0] == lo[0] && p2[0] == lo[0]) faces[n++] = 0;
  if (p1[0] == hi[0] && p2[0] == hi[0]) faces[n++] = 1;
  if (p1[1] == lo[1] && p2[1] == lo[1]) faces[n++] = 2;
  if (p1[1] == hi[1] && p2[1] == hi[1]) faces[n++] = 3;
  if (p1[2] == lo[2] && p2[2] == lo[2]) faces[n++] = 4;
  if (p1[2] == hi[2] && p2[2] == hi[2]) faces[n++] = 5;
  return n;
}


/* ----------------------------------------------------------------------
# extract 2d cell from iface (0-5) of 3d cell
# return lo2d/hi2d = xlo,xhi,ylo,yhi
# for XLO/XHI, keep (y,z) -> (x,y), look at face from inside/outside 3d cell
# for YLO/YHI, keep (x,z) -> (x,y), look at face from outside/inside 3d cell
# for ZLO/ZHI, keep (x,y) -> (x,y), look at face from inside/outside 3d cell
------------------------------------------------------------------------- */

void Cut3d::face_from_cell(int iface, double *lo2d, double *hi2d)
{
  if (iface < 2) {
    lo2d[0] = lo[1]; hi2d[0] = hi[1];
    lo2d[1] = lo[2]; hi2d[1] = hi[2];
  } else if (iface < 4) {
    lo2d[0] = lo[0]; hi2d[0] = hi[0];
    lo2d[1] = lo[2]; hi2d[1] = hi[2];
  } else {
    lo2d[0] = lo[0]; hi2d[0] = hi[0];
    lo2d[1] = lo[1]; hi2d[1] = hi[1];
  }
}

/* ----------------------------------------------------------------------
   compress a 3d pt into a 2d pt on iface
------------------------------------------------------------------------- */

void Cut3d::compress2d(int iface, double *p3, double *p2)
{
  if (iface < 2) {
    p2[0] = p3[1]; p2[1] = p3[2];
  } else if (iface < 4) {
    p2[0] = p3[0]; p2[1] = p3[2];
  } else {
    p2[0] = p3[0]; p2[1] = p3[1];
  }
}

/* ----------------------------------------------------------------------
   expand a 2d pt into 3d pt on iface with extra coord = value
------------------------------------------------------------------------- */

void Cut3d::expand2d(int iface, double value, double *p2, double *p3)
{
  if (iface < 2) {
    p3[0] = value; p3[1] = p2[0]; p3[2] = p2[1];
  } else if (iface < 4) {
    p3[0] = p2[0]; p3[1] = value; p3[2] = p2[1];
  } else {
    p3[0] = p2[0]; p3[1] = p2[1]; p3[2] = value;
  }
}

/* ----------------------------------------------------------------------
   look for edge (x,y) in list of edges
   match as (x,y) or (y,x)
   if flag, do not match edges that are part of a CTRI (style = CTRI,CTRIFACE)
     this is used by add_face() when adding edges of an entire face
     this avoids matching an on-face CTRI with norm into cell
   error if find edge and it is already part of a vertex in that dir
   return = index if find it, else -1
   return dir = 0 if matches as (x,y), 1 if matches as (y,x), -1 if no match
------------------------------------------------------------------------- */

int Cut3d::findedge(double *x, double *y, int flag, int &dir)
{
  double *p1,*p2;

  int nedge = edges.n;

  for (int i = 0; i < nedge; i++) {
    if (!edges[i].active) continue;
    if (flag && (edges[i].style == CTRI || edges[i].style == CTRIFACE)) 
      continue;
    p1 = edges[i].p1;
    p2 = edges[i].p2;
    if (samepoint(x,p1) && samepoint(y,p2)) {
      if (edges[i].nvert % 2 == 1) 
        error->one(FLERR,"Found edge in same direction");
      dir = 0;
      return i;
    }
    if (samepoint(x,p2) && samepoint(y,p1)) {
      if (edges[i].nvert / 2 == 1) 
        error->one(FLERR,"Found edge in same direction");
      dir = 1;
      return i;
    }
  }
  
  dir = -1;
  return -1;
}

/* ----------------------------------------------------------------------
   return intersection pt C of line segment A,B in dim with coord value
   guaranteed to intersect by caller
   C can be same as A or B, will just overwrite
------------------------------------------------------------------------- */
  
void Cut3d::between(double *a, double *b, int dim, double value, double *c)
{
  if (dim == 0) {
    c[1] = a[1] + (value-a[dim])/(b[dim]-a[dim]) * (b[1]-a[1]);
    c[2] = a[2] + (value-a[dim])/(b[dim]-a[dim]) * (b[2]-a[2]);
    c[0] = value;
  } else if (dim == 1) {
    c[0] = a[0] + (value-a[dim])/(b[dim]-a[dim]) * (b[0]-a[0]);
    c[2] = a[2] + (value-a[dim])/(b[dim]-a[dim]) * (b[2]-a[2]);
    c[1] = value;
  } else {
    c[0] = a[0] + (value-a[dim])/(b[dim]-a[dim]) * (b[0]-a[0]);
    c[1] = a[1] + (value-a[dim])/(b[dim]-a[dim]) * (b[1]-a[1]);
    c[2] = value;
  }
}

/* ----------------------------------------------------------------------
   return 1 if x,y are same point, else 0
------------------------------------------------------------------------- */
  
int Cut3d::samepoint(double *x, double *y)
{
  if (x[0] == y[0] && x[1] == y[1] && x[2] == y[2]) return 1;
  return 0;
}

/* ----------------------------------------------------------------------
   return 0-7 if pt is a corner pt of grid cell
   else return -1
------------------------------------------------------------------------- */
  
int Cut3d::corner(double *pt)
{
  if (pt[2] == lo[2]) {
    if (pt[1] == lo[1]) {
      if (pt[0] == lo[0]) return 0;
      else if (pt[0] == hi[0]) return 1;
    } else if (pt[1] == hi[1]) {
      if (pt[0] == lo[0]) return 2;
      else if (pt[0] == hi[0]) return 3;
    }
  } else if (pt[2] == hi[2]) {
    if (pt[1] == lo[1]) {
      if (pt[0] == lo[0]) return 4;
      else if (pt[0] == hi[0]) return 5;
    } else if (pt[1] == hi[1]) {
      if (pt[0] == lo[0]) return 6;
      else if (pt[0] == hi[0]) return 7;
    }
  }

  return -1;
}

/* ----------------------------------------------------------------------
   check if pt is inside or outside or on cell border
   return EXTERIOR,BORDER,INTERIOR
------------------------------------------------------------------------- */

int Cut3d::ptflag(double *pt)
{
  double x = pt[0];
  double y = pt[1];
  double z = pt[2];
  if (x < lo[0] || x > hi[0] || y < lo[1] || y > hi[1] ||
      z < lo[2] || z > hi[2]) return EXTERIOR;
  if (x > lo[0] && x < hi[0] && y > lo[1] && y < hi[1] &&
      z > lo[2] && z < hi[2]) return INTERIOR;
  return BORDER;
}

/* ----------------------------------------------------------------------
------------------------------------------------------------------------- */

void Cut3d::print_bpg(const char *str)
{
  int iedge,dir,newedge,newdir;

  printf("%s %d\n",str,id);
  printf("  Sizes: %d %d\n",verts.n,edges.n);

  printf("  Verts:\n");
  for (int i = 0; i < verts.n; i++) {
    if (verts[i].active == 0) continue;
    printf("   %d %d %d %d:",i,
           verts[i].active,verts[i].style,verts[i].label);
    printf(" [");
    iedge = verts[i].first;
    dir = verts[i].dirfirst;
    for (int j = 0; j < verts[i].nedge; j++) {
      printf("%d",iedge);
      if (j < verts[i].nedge-1) printf(" ");
      newedge = edges[iedge].next[dir];
      newdir = edges[iedge].dirnext[dir];
      iedge = newedge;
      dir = newdir;
    }
    printf("]");
    printf(" [");
    iedge = verts[i].first;
    dir = verts[i].dirfirst;
    for (int j = 0; j < verts[i].nedge; j++) {
      printf("%d",dir);
      if (j < verts[i].nedge-1) printf(" ");
      newedge = edges[iedge].next[dir];
      newdir = edges[iedge].dirnext[dir];
      iedge = newedge;
      dir = newdir;
    }
    printf("]");
    if (verts[i].norm) {
      printf(" [%g %g %g]\n",
             verts[i].norm[0],verts[i].norm[1],verts[i].norm[2]);
    } else printf(" [NULL]\n");
  }

  printf("  Edges:\n");
  for (int i = 0; i < edges.n; i++) {
    if (edges[i].active == 0) continue;
    printf("   %d %d %d",i,edges[i].active,edges[i].style);
    printf(" (%g %g %g)",edges[i].p1[0],edges[i].p1[1],edges[i].p1[2]);
    printf(" (%g %g %g)",edges[i].p2[0],edges[i].p2[1],edges[i].p2[2]);
    if (edges[i].nvert == 0) printf(" [-1]");
    if (edges[i].nvert == 1) {
      printf(" [%d]",edges[i].verts[0]);
      printf(" p1: [%d %d]",edges[i].prev[0],edges[i].dirprev[0]);
      printf(" n1: [%d %d]",edges[i].next[0],edges[i].dirnext[0]);
    }
    if (edges[i].nvert == 2) {
      printf(" [%d]",edges[i].verts[1]);
      printf(" p1: [%d %d]",edges[i].prev[1],edges[i].dirprev[1]);
      printf(" n1: [%d %d]",edges[i].next[1],edges[i].dirnext[1]);
    }
    if (edges[i].nvert == 3) {
      printf(" [%d %d]",edges[i].verts[0],edges[i].verts[1]);
      printf(" p1: [%d %d]",edges[i].prev[0],edges[i].dirprev[0]);
      printf(" n1: [%d %d]",edges[i].next[0],edges[i].dirnext[0]);
      printf(" p2: [%d %d]",edges[i].prev[1],edges[i].dirprev[1]);
      printf(" n2: [%d %d]",edges[i].next[1],edges[i].dirnext[1]);
    }
    if (edges[i].nvert > 3) printf(" [BIG %d]",edges[i].nvert);
    printf("\n");
  }
}

/* ----------------------------------------------------------------------
------------------------------------------------------------------------- */

void Cut3d::print_loops()
{
  printf("LOOP %d\n",id);
  printf("  loops %d\n",loops.n);
  for (int i = 0; i < loops.n; i++) {
    printf("  loop %d\n",i);
    printf("    flag %d\n",loops[i].flag);
    printf("    volume %g\n",loops[i].volume);
    printf("    nverts %d\n",loops[i].n);
    printf("    verts: [");
    int ivert = loops[i].first;
    for (int j = 0; j < loops[i].n; j++) {
      printf("%d ",ivert);
      ivert = verts[ivert].next;
    }
    printf("]\n");
  }
}


// ----------------------------------------------------------------------
// ----------------------------------------------------------------------
// ----------------------------------------------------------------------
// old methods, can delete at some point
// ----------------------------------------------------------------------
// ----------------------------------------------------------------------
// ----------------------------------------------------------------------

/*
// clip test of triangle PQR against cell with corners LO,HI
// return 1 if intersects, 0 if not

int Cut3d::tri_hex_intersect(double *v0, double *v1, double *v2, double *norm)
{
  double xlo = lo[0];
  double xhi = hi[0];
  double ylo = lo[1];
  double yhi = hi[1];
  double zlo = lo[2];
  double zhi = hi[2];

  // if any of 3 tri vertices are inside hex, intersection
  // use <= and >= so touching hex surface is same as inside it
  // important to do this test first, b/c whichside() test can be epsilon off

  if (v0[0] >= xlo && v0[0] <= xhi && v0[1] >= ylo && v0[1] <= yhi &&
      v0[2] >= zlo && v0[2] <= zhi) return 1;

  if (v1[0] >= xlo && v1[0] <= xhi && v1[1] >= ylo && v1[1] <= yhi &&
      v1[2] >= zlo && v1[2] <= zhi) return 1;

  if (v2[0] >= xlo && v2[0] <= xhi && v2[1] >= ylo && v2[1] <= yhi &&
      v2[2] >= zlo && v2[2] <= zhi) return 1;

  // if all 3 tri pts are on wrong side of any hex plane, no intersection
  // NOTE: think this is redundant with caller

  //if (v0[0] < xlo && v1[0] < xlo && v2[0] < xlo) return 0;
  //if (v0[0] > xhi && v1[0] > xhi && v2[0] > xhi) return 0;
  //if (v0[1] < ylo && v1[1] < ylo && v2[1] < ylo) return 0;
  //if (v0[1] > yhi && v1[1] > yhi && v2[1] > yhi) return 0;
  //if (v0[2] < zlo && v1[2] < zlo && v2[2] < zlo) return 0;
  //if (v0[2] > zhi && v1[2] > zhi && v2[2] > zhi) return 0;

  // if all 8 hex pts are on same side of tri plane, no intersection
  // NOTE: not sure why needed, but speeds things up

  int sum = 0;
  sum += whichside(v0,norm,xlo,ylo,zlo);
  sum += whichside(v0,norm,xhi,ylo,zlo);
  sum += whichside(v0,norm,xlo,yhi,zlo);
  sum += whichside(v0,norm,xhi,yhi,zlo);
  sum += whichside(v0,norm,xlo,ylo,zhi);
  sum += whichside(v0,norm,xhi,ylo,zhi);
  sum += whichside(v0,norm,xlo,yhi,zhi);
  sum += whichside(v0,norm,xhi,yhi,zhi);
  if (sum == 8 || sum == -8) return 0;

  // test 12 hex edges for intersection with tri
  // b,e = begin/end of hex edge line segment

  double b[3],e[3];

  b[0] = xlo;   b[1] = ylo;   b[2] = zlo;
  e[0] = xhi;   e[1] = ylo;   e[2] = zlo;
  if (line_tri_intersect(b,e,v0,v1,v2)) return 1;

  b[0] = xlo;   b[1] = yhi;   b[2] = zlo;
  e[0] = xhi;   e[1] = yhi;   e[2] = zlo;
  if (line_tri_intersect(b,e,v0,v1,v2)) return 1;

  b[0] = xlo;   b[1] = ylo;   b[2] = zhi;
  e[0] = xhi;   e[1] = ylo;   e[2] = zhi;
  if (line_tri_intersect(b,e,v0,v1,v2)) return 1;

  b[0] = xlo;   b[1] = yhi;   b[2] = zhi;
  e[0] = xhi;   e[1] = yhi;   e[2] = zhi;
  if (line_tri_intersect(b,e,v0,v1,v2)) return 1;

  b[0] = xlo;   b[1] = ylo;   b[2] = zlo;
  e[0] = xlo;   e[1] = yhi;   e[2] = zlo;
  if (line_tri_intersect(b,e,v0,v1,v2)) return 1;

  b[0] = xhi;   b[1] = ylo;   b[2] = zlo;
  e[0] = xhi;   e[1] = yhi;   e[2] = zlo;
  if (line_tri_intersect(b,e,v0,v1,v2)) return 1;

  b[0] = xlo;   b[1] = ylo;   b[2] = zhi;
  e[0] = xlo;   e[1] = yhi;   e[2] = zhi;
  if (line_tri_intersect(b,e,v0,v1,v2)) return 1;

  b[0] = xhi;   b[1] = ylo;   b[2] = zhi;
  e[0] = xhi;   e[1] = yhi;   e[2] = zhi;
  if (line_tri_intersect(b,e,v0,v1,v2)) return 1;

  b[0] = xlo;   b[1] = ylo;   b[2] = zlo;
  e[0] = xlo;   e[1] = ylo;   e[2] = zhi;
  if (line_tri_intersect(b,e,v0,v1,v2)) return 1;

  b[0] = xhi;   b[1] = ylo;   b[2] = zlo;
  e[0] = xhi;   e[1] = ylo;   e[2] = zhi;
  if (line_tri_intersect(b,e,v0,v1,v2)) return 1;

  b[0] = xlo;   b[1] = yhi;   b[2] = zlo;
  e[0] = xlo;   e[1] = yhi;   e[2] = zhi;
  if (line_tri_intersect(b,e,v0,v1,v2)) return 1;

  b[0] = xhi;   b[1] = yhi;   b[2] = zlo;
  e[0] = xhi;   e[1] = yhi;   e[2] = zhi;
  if (line_tri_intersect(b,e,v0,v1,v2)) return 1;

  // test 3 tri edges for intersection with 6 faces of hex
  // h0,h1,h2,h3 = 4 corner pts of hex face
  // n = normal to xyz faces, depends on vertex ordering
  // each face is treated as 2 triangles -> 6 tests per face

  double h0[3],h1[3],h2[3],h3[3];
  
  h0[0] = xlo;  h0[1] = ylo;  h0[2] = zlo;
  h1[0] = xlo;  h1[1] = yhi;  h1[2] = zlo;
  h2[0] = xlo;  h2[1] = yhi;  h2[2] = zhi;
  h3[0] = xlo;  h3[1] = ylo;  h3[2] = zhi;

  if (line_tri_intersect(v0,v1,h0,h1,h2) ||
      line_tri_intersect(v1,v2,h0,h1,h2) ||
      line_tri_intersect(v2,v0,h0,h1,h2) ||
      line_tri_intersect(v0,v1,h0,h2,h3) ||
      line_tri_intersect(v1,v2,h0,h2,h3) ||
      line_tri_intersect(v2,v0,h0,h2,h3)) return 1;


  h0[0] = h1[0] = h2[0] = h3[0] = xhi;

  if (line_tri_intersect(v0,v1,h0,h1,h2) ||
      line_tri_intersect(v1,v2,h0,h1,h2) ||
      line_tri_intersect(v2,v0,h0,h1,h2) ||
      line_tri_intersect(v0,v1,h0,h2,h3) ||
      line_tri_intersect(v1,v2,h0,h2,h3) ||
      line_tri_intersect(v2,v0,h0,h2,h3)) return 1;

  h0[0] = xlo;  h0[1] = ylo;  h0[2] = zlo;
  h1[0] = xhi;  h1[1] = ylo;  h1[2] = zlo;
  h2[0] = xhi;  h2[1] = ylo;  h2[2] = zhi;
  h3[0] = xlo;  h3[1] = ylo;  h3[2] = zhi;
  
  if (line_tri_intersect(v0,v1,h0,h1,h2) ||
      line_tri_intersect(v1,v2,h0,h1,h2) ||
      line_tri_intersect(v2,v0,h0,h1,h2) ||
      line_tri_intersect(v0,v1,h0,h2,h3) ||
      line_tri_intersect(v1,v2,h0,h2,h3) ||
      line_tri_intersect(v2,v0,h0,h2,h3)) return 1;

  h0[1] = h1[1] = h2[1] = h3[1] = yhi;

  if (line_tri_intersect(v0,v1,h0,h1,h2) ||
      line_tri_intersect(v1,v2,h0,h1,h2) ||
      line_tri_intersect(v2,v0,h0,h1,h2) ||
      line_tri_intersect(v0,v1,h0,h2,h3) ||
      line_tri_intersect(v1,v2,h0,h2,h3) ||
      line_tri_intersect(v2,v0,h0,h2,h3)) return 1;

  h0[0] = xlo;  h0[1] = ylo;  h0[2] = zlo;
  h1[0] = xhi;  h1[1] = ylo;  h1[2] = zlo;
  h2[0] = xhi;  h2[1] = yhi;  h2[2] = zlo;
  h3[0] = xlo;  h3[1] = yhi;  h3[2] = zlo;
  
  if (line_tri_intersect(v0,v1,h0,h1,h2) ||
      line_tri_intersect(v1,v2,h0,h1,h2) ||
      line_tri_intersect(v2,v0,h0,h1,h2) ||
      line_tri_intersect(v0,v1,h0,h2,h3) ||
      line_tri_intersect(v1,v2,h0,h2,h3) ||
      line_tri_intersect(v2,v0,h0,h2,h3)) return 1;

  h0[2] = h1[2] = h2[2] = h3[2] = zhi;
  
  if (line_tri_intersect(v0,v1,h0,h1,h2) ||
      line_tri_intersect(v1,v2,h0,h1,h2) ||
      line_tri_intersect(v2,v0,h0,h1,h2) ||
      line_tri_intersect(v0,v1,h0,h2,h3) ||
      line_tri_intersect(v1,v2,h0,h2,h3) ||
      line_tri_intersect(v2,v0,h0,h2,h3)) return 1;

  return 0;
}

int Cut3d::line_tri_intersect(double *start, double *stop,
                              double *v0, double *v1, double *v2)
{
  // no intersection if both tetvol > 0 or both tetvol < 0

  int sum = 0;
  sum += tetvol(start,v0,v1,v2);
  sum += tetvol(stop,v0,v1,v2);
  if (sum == 2 || sum == -2) return 0;

  // no intersection if 
  // one tetvol = 0.0 if intersection is on tri edge
  // two tetvols = 0.0 if intersection is on tri corner

  int tv1 = tetvol(start,v0,v1,stop);
  int tv2 = tetvol(start,v1,v2,stop);
  int tv3 = tetvol(start,v2,v0,stop);

  if (tv1 == 0 && tv2 == 0 && tv3 == 0) return 0;

  if (tv1 == 0) {
    if (tv2 == 0 || tv3 == 0) return 1;
    sum = tv2 + tv3;
    if (sum == 2 || sum == -2) return 1;
    return 0;
  }

  if (tv2 == 0) {
    if (tv1 == 0 || tv3 == 0) return 1;
    sum = tv1 + tv3;
    if (sum == 2 || sum == -2) return 1;
    return 0;
  }

  if (tv3 == 0) {
    if (tv1 == 0 || tv2 == 0) return 1;
    sum = tv1 + tv2;
    if (sum == 2 || sum == -2) return 1;
    return 0;
  }

  sum = tv1 + tv2 + tv3;
  if (sum == 3 || sum == -3) return 1;
  return 0;
}

// determine which side of plane the point x,y,z is on
// plane is defined by vertex pt v and unit normal vec
// return -1,0,1 for below,on,above plane

int Cut3d::whichside(double *v, double *norm, double x, double y, double z)
{
  double vec[3];
  vec[0] = x - v[0];
  vec[1] = y - v[1];
  vec[2] = z - v[2];

  double dotproduct = MathExtra::dot3(norm,vec);
  if (dotproduct < 0.0) return -1;
  else if (dotproduct > 0.0) return 1;
  else return 0;
}

int Cut3d::tetvol(double *v0, double *v1, double *v2, double *v3)
{
  double det4 = -det3(v1,v2,v3) + det3(v0,v2,v3) - 
    det3(v0,v1,v3) + det3(v0,v1,v2);
  if (det4 < 0.0) return -1;
  if (det4 > 0.0) return 1;
  return 0;
}

double Cut3d::det3(double *a, double *b, double *c)
{
  double det = 0.0;
  det += a[0] * (b[1]*c[2] - b[2]*c[1]);
  det -= a[1] * (b[0]*c[2] - b[2]*c[0]);
  det += a[2] * (b[0]*c[1] - b[1]*c[0]);
  return det;
}

*/
