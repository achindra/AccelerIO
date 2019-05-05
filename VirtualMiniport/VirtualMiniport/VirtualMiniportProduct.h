/*++

Module Name:

    VirtualMiniportProduct.h

Date:

    4-Jan-2014

Abstract:

    Header contains the definitions for product description

--*/

#ifndef __VIRTUAL_MINIPORT_PRODUCT_H_
#define __VIRTUAL_MINIPORT_PRODUCT_H_

#define VIRTUAL_MINIPORT_PRODUCT_CONFIGURATION_STRING_LENGTH 8 // 

//
// 8 Bytes in length
//
#define VIRTUAL_MINIPORT_VENDORID_STRING L"Msft"
#define VIRTUAL_MINIPORT_VENDORID_STRING_ANSI "Msft"

//
// SVM - Storage Virtual Miniport
// 8 Bytes in length
//
#define VIRTUAL_MINIPORT_PRODUCTID_STRING L"SVM"
#define VIRTUAL_MINIPORT_PRODUCTID_STRING_ANSI "SVM"

//
// 4 bytes in size
//
#define VIRTUAL_MINIPORT_PRODUCT_REVISION_STRING L"1000"
#define VIRTUAL_MINIPORT_PRODUCT_REVISION_STRING_ANSI "1000"

#endif //__VIRTUAL_MINIPORT_PRODUCT_H_
