/******************************************************************************
 * $Id: pj_transform.c 1504 2009-01-06 02:11:57Z warmerdam $
 *
 * Project:  PROJ.4
 * Purpose:  Perform overall coordinate system to coordinate system 
 *           transformations (pj_transform() function) including reprojection
 *           and datum shifting.
 * Author:   Frank Warmerdam, warmerdam@pobox.com
 *
 ******************************************************************************
 * Copyright (c) 2000, Frank Warmerdam
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

#include "projects.h"
#include <string.h>
#include <math.h>
#include "geocent.h"

PJ_CVSID("$Id: pj_transform.c 1504 2009-01-06 02:11:57Z warmerdam $");

#ifndef SRS_WGS84_SEMIMAJOR
#define SRS_WGS84_SEMIMAJOR 6378137.0
#endif

#ifndef SRS_WGS84_ESQUARED
#define SRS_WGS84_ESQUARED 0.0066943799901413165
#endif

#define Dx_BF (defn->datum_params[0])
#define Dy_BF (defn->datum_params[1])
#define Dz_BF (defn->datum_params[2])
#define Rx_BF (defn->datum_params[3])
#define Ry_BF (defn->datum_params[4])
#define Rz_BF (defn->datum_params[5])
#define M_BF  (defn->datum_params[6])

/* 
** This table is intended to indicate for any given error code in 
** the range 0 to -44, whether that error will occur for all locations (ie.
** it is a problem with the coordinate system as a whole) in which case the
** value would be 0, or if the problem is with the point being transformed
** in which case the value is 1. 
**
** At some point we might want to move this array in with the error message
** list or something, but while experimenting with it this should be fine. 
*/

static const int transient_error[45] = {
    /*             0  1  2  3  4  5  6  7  8  9   */
    /* 0 to 9 */   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    /* 10 to 19 */ 0, 0, 0, 0, 1, 1, 0, 1, 1, 1,  
    /* 20 to 29 */ 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 
    /* 30 to 39 */ 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 
    /* 40 to 44 */ 0, 0, 0, 0, 0 };

/************************************************************************/
/*                            pj_transform()                            */
/*                                                                      */
/*      Currently this function doesn't recognise if two projections    */
/*      are identical (to short circuit reprojection) because it is     */
/*      difficult to compare PJ structures (since there are some        */
/*      projection specific components).                                */
/************************************************************************/

int pj_transform( PJ *srcdefn, PJ *dstdefn, long point_count, int point_offset,
                  double *x, double *y, double *z )

{
    long      i;
   
    pj_errno = 0;

    if( point_offset == 0 )
        point_offset = 1;

/* -------------------------------------------------------------------- */
/*      Transform geocentric source coordinates to lat/long.            */
/* -------------------------------------------------------------------- */
    if( srcdefn->is_geocent )
    {
        if( z == NULL )
        {
            pj_errno = PJD_ERR_GEOCENTRIC;
            return PJD_ERR_GEOCENTRIC;
        }

        if( srcdefn->to_meter != 1.0 )
        {
            for( i = 0; i < point_count; i++ )
            {
                if( x[point_offset*i] != HUGE_VAL )
                {
                    x[point_offset*i] *= srcdefn->to_meter;
                    y[point_offset*i] *= srcdefn->to_meter;
                }
            }
        }

        if( pj_geocentric_to_geodetic( srcdefn->a_orig, srcdefn->es_orig,
                                       point_count, point_offset, 
                                       x, y, z ) != 0) 
            return pj_errno;
    }

/* -------------------------------------------------------------------- */
/*      Transform source points to lat/long, if they aren't             */
/*      already.                                                        */
/* -------------------------------------------------------------------- */
    else if( !srcdefn->is_latlong )
    {
        if( srcdefn->inv == NULL )
        {
            pj_errno = -17; /* this isn't correct, we need a no inverse err */
            if( getenv( "PROJ_DEBUG" ) != NULL )
            {
                fprintf( stderr, 
                       "pj_transform(): source projection not invertable\n" );
            }
            return pj_errno;
        }

        for( i = 0; i < point_count; i++ )
        {
            XY         projected_loc;
            LP	       geodetic_loc;

            projected_loc.u = x[point_offset*i];
            projected_loc.v = y[point_offset*i];

            if( projected_loc.u == HUGE_VAL )
                continue;

            geodetic_loc = pj_inv( projected_loc, srcdefn );
            if( pj_errno != 0 )
            {
                if( (pj_errno != 33 /*EDOM*/ && pj_errno != 34 /*ERANGE*/ )
                    && (pj_errno > 0 || pj_errno < -44 || point_count == 1
                        || transient_error[-pj_errno] == 0 ) )
                    return pj_errno;
                else
                {
                    geodetic_loc.u = HUGE_VAL;
                    geodetic_loc.v = HUGE_VAL;
                }
            }

            x[point_offset*i] = geodetic_loc.u;
            y[point_offset*i] = geodetic_loc.v;
        }
    }
/* -------------------------------------------------------------------- */
/*      But if they are already lat long, adjust for the prime          */
/*      meridian if there is one in effect.                             */
/* -------------------------------------------------------------------- */
    if( srcdefn->from_greenwich != 0.0 )
    {
        for( i = 0; i < point_count; i++ )
        {
            if( x[point_offset*i] != HUGE_VAL )
                x[point_offset*i] += srcdefn->from_greenwich;
        }
    }

/* -------------------------------------------------------------------- */
/*      Convert datums if needed, and possible.                         */
/* -------------------------------------------------------------------- */
    if( pj_datum_transform( srcdefn, dstdefn, point_count, point_offset, 
                            x, y, z ) != 0 )
        return pj_errno;

/* -------------------------------------------------------------------- */
/*      But if they are staying lat long, adjust for the prime          */
/*      meridian if there is one in effect.                             */
/* -------------------------------------------------------------------- */
    if( dstdefn->from_greenwich != 0.0 )
    {
        for( i = 0; i < point_count; i++ )
        {
            if( x[point_offset*i] != HUGE_VAL )
                x[point_offset*i] -= dstdefn->from_greenwich;
        }
    }


/* -------------------------------------------------------------------- */
/*      Transform destination latlong to geocentric if required.        */
/* -------------------------------------------------------------------- */
    if( dstdefn->is_geocent )
    {
        if( z == NULL )
        {
            pj_errno = PJD_ERR_GEOCENTRIC;
            return PJD_ERR_GEOCENTRIC;
        }

        pj_geodetic_to_geocentric( dstdefn->a_orig, dstdefn->es_orig,
                                   point_count, point_offset, x, y, z );

        if( dstdefn->fr_meter != 1.0 )
        {
            for( i = 0; i < point_count; i++ )
            {
                if( x[point_offset*i] != HUGE_VAL )
                {
                    x[point_offset*i] *= dstdefn->fr_meter;
                    y[point_offset*i] *= dstdefn->fr_meter;
                }
            }
        }
    }

/* -------------------------------------------------------------------- */
/*      Transform destination points to projection coordinates, if      */
/*      desired.                                                        */
/* -------------------------------------------------------------------- */
    else if( !dstdefn->is_latlong )
    {
        for( i = 0; i < point_count; i++ )
        {
            XY         projected_loc;
            LP	       geodetic_loc;

            geodetic_loc.u = x[point_offset*i];
            geodetic_loc.v = y[point_offset*i];

            if( geodetic_loc.u == HUGE_VAL )
                continue;

            projected_loc = pj_fwd( geodetic_loc, dstdefn );
            if( pj_errno != 0 )
            {
                if( (pj_errno != 33 /*EDOM*/ && pj_errno != 34 /*ERANGE*/ )
                    && (pj_errno > 0 || pj_errno < -44 || point_count == 1
                        || transient_error[-pj_errno] == 0 ) )
                    return pj_errno;
                else
                {
                    projected_loc.u = HUGE_VAL;
                    projected_loc.v = HUGE_VAL;
                }
            }

            x[point_offset*i] = projected_loc.u;
            y[point_offset*i] = projected_loc.v;
        }
    }

/* -------------------------------------------------------------------- */
/*      If a wrapping center other than 0 is provided, rewrap around    */
/*      the suggested center (for latlong coordinate systems only).     */
/* -------------------------------------------------------------------- */
    else if( dstdefn->is_latlong && dstdefn->long_wrap_center != 0 )
    {
        for( i = 0; i < point_count; i++ )
        {
            if( x[point_offset*i] == HUGE_VAL )
                continue;

            while( x[point_offset*i] < dstdefn->long_wrap_center - HALFPI )
                x[point_offset*i] += PI;
            while( x[point_offset*i] > dstdefn->long_wrap_center + HALFPI )
                x[point_offset*i] -= PI;
        }
    }

    return 0;
}

/************************************************************************/
/*                     pj_geodetic_to_geocentric()                      */
/************************************************************************/

int pj_geodetic_to_geocentric( double a, double es, 
                               long point_count, int point_offset,
                               double *x, double *y, double *z )

{
    double b;
    int    i;
    GeocentricInfo gi;

    pj_errno = 0;

    if( es == 0.0 )
        b = a;
    else
        b = a * sqrt(1-es);

    if( pj_Set_Geocentric_Parameters( &gi, a, b ) != 0 )
    {
        pj_errno = PJD_ERR_GEOCENTRIC;
        return pj_errno;
    }

    for( i = 0; i < point_count; i++ )
    {
        long io = i * point_offset;

        if( x[io] == HUGE_VAL  )
            continue;

        if( pj_Convert_Geodetic_To_Geocentric( &gi, y[io], x[io], z[io], 
                                               x+io, y+io, z+io ) != 0 )
        {
            pj_errno = -14;
            x[io] = y[io] = HUGE_VAL;
            /* but keep processing points! */
        }
    }

    return pj_errno;
}

/************************************************************************/
/*                     pj_geodetic_to_geocentric()                      */
/************************************************************************/

int pj_geocentric_to_geodetic( double a, double es, 
                               long point_count, int point_offset,
                               double *x, double *y, double *z )

{
    double b;
    int    i;
    GeocentricInfo gi;

    if( es == 0.0 )
        b = a;
    else
        b = a * sqrt(1-es);

    if( pj_Set_Geocentric_Parameters( &gi, a, b ) != 0 )
    {
        pj_errno = PJD_ERR_GEOCENTRIC;
        return pj_errno;
    }

    for( i = 0; i < point_count; i++ )
    {
        long io = i * point_offset;

        if( x[io] == HUGE_VAL )
            continue;

        pj_Convert_Geocentric_To_Geodetic( &gi, x[io], y[io], z[io], 
                                           y+io, x+io, z+io );
    }

    return 0;
}

/************************************************************************/
/*                         pj_compare_datums()                          */
/*                                                                      */
/*      Returns TRUE if the two datums are identical, otherwise         */
/*      FALSE.                                                          */
/************************************************************************/

int pj_compare_datums( PJ *srcdefn, PJ *dstdefn )

{
    if( srcdefn->datum_type != dstdefn->datum_type )
    {
        return 0;
    }
    else if( srcdefn->a_orig != dstdefn->a_orig 
             || ABS(srcdefn->es_orig - dstdefn->es_orig) > 0.000000000050 )
    {
        /* the tolerence for es is to ensure that GRS80 and WGS84 are
           considered identical */
        return 0;
    }
    else if( srcdefn->datum_type == PJD_3PARAM )
    {
        return (srcdefn->datum_params[0] == dstdefn->datum_params[0]
                && srcdefn->datum_params[1] == dstdefn->datum_params[1]
                && srcdefn->datum_params[2] == dstdefn->datum_params[2]);
    }
    else if( srcdefn->datum_type == PJD_7PARAM )
    {
        return (srcdefn->datum_params[0] == dstdefn->datum_params[0]
                && srcdefn->datum_params[1] == dstdefn->datum_params[1]
                && srcdefn->datum_params[2] == dstdefn->datum_params[2]
                && srcdefn->datum_params[3] == dstdefn->datum_params[3]
                && srcdefn->datum_params[4] == dstdefn->datum_params[4]
                && srcdefn->datum_params[5] == dstdefn->datum_params[5]
                && srcdefn->datum_params[6] == dstdefn->datum_params[6]);
    }
    else if( srcdefn->datum_type == PJD_GRIDSHIFT )
    {
        return strcmp( pj_param(srcdefn->params,"snadgrids").s,
                       pj_param(dstdefn->params,"snadgrids").s ) == 0;
    }
    else
        return 1;
}

/************************************************************************/
/*                       pj_geocentic_to_wgs84()                        */
/************************************************************************/

int pj_geocentric_to_wgs84( PJ *defn, 
                            long point_count, int point_offset,
                            double *x, double *y, double *z )

{
    int       i;

    pj_errno = 0;

    if( defn->datum_type == PJD_3PARAM )
    {
        for( i = 0; i < point_count; i++ )
        {
            long io = i * point_offset;
            
            if( x[io] == HUGE_VAL )
                continue;

            x[io] = x[io] + Dx_BF;
            y[io] = y[io] + Dy_BF;
            z[io] = z[io] + Dz_BF;
        }
    }
    else if( defn->datum_type == PJD_7PARAM )
    {
        for( i = 0; i < point_count; i++ )
        {
            long io = i * point_offset;
            double x_out, y_out, z_out;

            if( x[io] == HUGE_VAL )
                continue;

            x_out = M_BF*(       x[io] - Rz_BF*y[io] + Ry_BF*z[io]) + Dx_BF;
            y_out = M_BF*( Rz_BF*x[io] +       y[io] - Rx_BF*z[io]) + Dy_BF;
            z_out = M_BF*(-Ry_BF*x[io] + Rx_BF*y[io] +       z[io]) + Dz_BF;

            x[io] = x_out;
            y[io] = y_out;
            z[io] = z_out;
        }
    }

    return 0;
}

/************************************************************************/
/*                      pj_geocentic_from_wgs84()                       */
/************************************************************************/

int pj_geocentric_from_wgs84( PJ *defn, 
                              long point_count, int point_offset,
                              double *x, double *y, double *z )

{
    int       i;

    pj_errno = 0;

    if( defn->datum_type == PJD_3PARAM )
    {
        for( i = 0; i < point_count; i++ )
        {
            long io = i * point_offset;

            if( x[io] == HUGE_VAL )
                continue;
            
            x[io] = x[io] - Dx_BF;
            y[io] = y[io] - Dy_BF;
            z[io] = z[io] - Dz_BF;
        }
    }
    else if( defn->datum_type == PJD_7PARAM )
    {
        for( i = 0; i < point_count; i++ )
        {
            long io = i * point_offset;
            double x_tmp, y_tmp, z_tmp;

            if( x[io] == HUGE_VAL )
                continue;

            x_tmp = (x[io] - Dx_BF) / M_BF;
            y_tmp = (y[io] - Dy_BF) / M_BF;
            z_tmp = (z[io] - Dz_BF) / M_BF;

            x[io] =        x_tmp + Rz_BF*y_tmp - Ry_BF*z_tmp;
            y[io] = -Rz_BF*x_tmp +       y_tmp + Rx_BF*z_tmp;
            z[io] =  Ry_BF*x_tmp - Rx_BF*y_tmp +       z_tmp;
        }
    }

    return 0;
}

/************************************************************************/
/*                         pj_datum_transform()                         */
/*                                                                      */
/*      The input should be long/lat/z coordinates in radians in the    */
/*      source datum, and the output should be long/lat/z               */
/*      coordinates in radians in the destination datum.                */
/************************************************************************/

int pj_datum_transform( PJ *srcdefn, PJ *dstdefn, 
                        long point_count, int point_offset,
                        double *x, double *y, double *z )

{
    double      src_a, src_es, dst_a, dst_es;
    int         z_is_temp = FALSE;

    pj_errno = 0;

/* -------------------------------------------------------------------- */
/*      We cannot do any meaningful datum transformation if either      */
/*      the source or destination are of an unknown datum type          */
/*      (ie. only a +ellps declaration, no +datum).  This is new        */
/*      behavior for PROJ 4.6.0.                                        */
/* -------------------------------------------------------------------- */
    if( srcdefn->datum_type == PJD_UNKNOWN
        || dstdefn->datum_type == PJD_UNKNOWN )
        return 0;

/* -------------------------------------------------------------------- */
/*      Short cut if the datums are identical.                          */
/* -------------------------------------------------------------------- */
    if( pj_compare_datums( srcdefn, dstdefn ) )
        return 0;

    src_a = srcdefn->a_orig;
    src_es = srcdefn->es_orig;

    dst_a = dstdefn->a_orig;
    dst_es = dstdefn->es_orig;

/* -------------------------------------------------------------------- */
/*      Create a temporary Z array if one is not provided.              */
/* -------------------------------------------------------------------- */
    if( z == NULL )
    {
        int	bytes = sizeof(double) * point_count * point_offset;
        z = (double *) pj_malloc(bytes);
        memset( z, 0, bytes );
        z_is_temp = TRUE;
    }

#define CHECK_RETURN {if( pj_errno != 0 && (pj_errno > 0 || transient_error[-pj_errno] == 0) ) { if( z_is_temp ) pj_dalloc(z); return pj_errno; }}

/* -------------------------------------------------------------------- */
/*	If this datum requires grid shifts, then apply it to geodetic   */
/*      coordinates.                                                    */
/* -------------------------------------------------------------------- */
    if( srcdefn->datum_type == PJD_GRIDSHIFT )
    {
        pj_apply_gridshift( pj_param(srcdefn->params,"snadgrids").s, 0, 
                            point_count, point_offset, x, y, z );
        CHECK_RETURN;

        src_a = SRS_WGS84_SEMIMAJOR;
        src_es = SRS_WGS84_ESQUARED;
    }

    if( dstdefn->datum_type == PJD_GRIDSHIFT )
    {
        dst_a = SRS_WGS84_SEMIMAJOR;
        dst_es = SRS_WGS84_ESQUARED;
    }
        
/* ==================================================================== */
/*      Do we need to go through geocentric coordinates?                */
/* ==================================================================== */
    if( src_es != dst_es || src_a != dst_a
        || srcdefn->datum_type == PJD_3PARAM 
        || srcdefn->datum_type == PJD_7PARAM
        || dstdefn->datum_type == PJD_3PARAM 
        || dstdefn->datum_type == PJD_7PARAM)
    {
/* -------------------------------------------------------------------- */
/*      Convert to geocentric coordinates.                              */
/* -------------------------------------------------------------------- */
        pj_geodetic_to_geocentric( src_a, src_es,
                                   point_count, point_offset, x, y, z );
        CHECK_RETURN;

/* -------------------------------------------------------------------- */
/*      Convert between datums.                                         */
/* -------------------------------------------------------------------- */
        if( srcdefn->datum_type == PJD_3PARAM 
            || srcdefn->datum_type == PJD_7PARAM )
        {
            pj_geocentric_to_wgs84( srcdefn, point_count, point_offset,x,y,z);
            CHECK_RETURN;
        }

        if( dstdefn->datum_type == PJD_3PARAM 
            || dstdefn->datum_type == PJD_7PARAM )
        {
            pj_geocentric_from_wgs84( dstdefn, point_count,point_offset,x,y,z);
            CHECK_RETURN;
        }

/* -------------------------------------------------------------------- */
/*      Convert back to geodetic coordinates.                           */
/* -------------------------------------------------------------------- */
        pj_geocentric_to_geodetic( dst_a, dst_es,
                                   point_count, point_offset, x, y, z );
        CHECK_RETURN;
    }

/* -------------------------------------------------------------------- */
/*      Apply grid shift to destination if required.                    */
/* -------------------------------------------------------------------- */
    if( dstdefn->datum_type == PJD_GRIDSHIFT )
    {
        pj_apply_gridshift( pj_param(dstdefn->params,"snadgrids").s, 1,
                            point_count, point_offset, x, y, z );
        CHECK_RETURN;
    }

    if( z_is_temp )
        pj_dalloc( z );

    return 0;
}

