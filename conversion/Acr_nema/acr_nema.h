/* ----------------------------- MNI Header -----------------------------------
@NAME       : acr_nema.h
@DESCRIPTION: Header file for ACR-NEMA code.
@METHOD     : 
@GLOBALS    : 
@CREATED    : November 10, 1993 (Peter Neelin)
@MODIFIED   : $Log: acr_nema.h,v $
@MODIFIED   : Revision 1.5  1994-09-23 16:42:37  neelin
@MODIFIED   : Changed acr_nema_io to acr_io and acr_nema_test to acr_test.
@MODIFIED   :
 * Revision 1.4  94/01/06  13:31:27  neelin
 * Changed acr_need_invert to a public function.
 * 
 * Revision 1.3  93/12/08  09:07:02  neelin
 * 
 * Revision 1.2  93/11/24  11:26:09  neelin
 * Added TRUE and FALSE.
 * 
 * Revision 1.1  93/11/19  12:49:34  neelin
 * Initial revision
 * 
@COPYRIGHT  :
              Copyright 1993 Peter Neelin, McConnell Brain Imaging Centre, 
              Montreal Neurological Institute, McGill University.
              Permission to use, copy, modify, and distribute this
              software and its documentation for any purpose and without
              fee is hereby granted, provided that the above copyright
              notice appear in all copies.  The author and McGill University
              make no representations about the suitability of this
              software for any purpose.  It is provided "as is" without
              express or implied warranty.
---------------------------------------------------------------------------- */

/* Define constants */
#ifndef public
#  define public
#endif
#ifndef private
#  define private static
#endif

#ifndef TRUE
#  define TRUE 1
#endif
#ifndef FALSE
#  define FALSE 0
#endif

/* Include files */
#include <file_io.h>
#include <acr_io.h>
#include <element.h>
#include <group.h>
#include <message.h>
