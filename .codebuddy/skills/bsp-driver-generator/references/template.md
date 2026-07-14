# BSP Driver File Template (AnFuLi V5 Style)

When generating `bsp_xxx.c` / `bsp_xxx.h`, replace `xxx` with the peripheral name (lowercase) and `Xxx` with its capitalized form.

## bsp_xxx.c

```c
/*
*********************************************************************************************************
*
*   Module Name : XXX driver module
*   File Name   : bsp_xxx.c
*   Version     : V1.0
*   Description : Brief description of the peripheral driver's features and usage.
*
*   Revision History:
*       Version   Date        Author   Description
*       V1.0    2026-xx-xx  User     Initial release
*
*   Copyright (C), 2026, AnFuLi Electronics www.armfly.com
*
*********************************************************************************************************
*/

#include "bsp.h"

/*
*********************************************************************************************************
*   Function   : bsp_InitXxx
*   Description : Initialize the XXX peripheral. Entry point called by bsp_Init(),
*                 it calls bsp_InitHardXxx() and bsp_InitVarXxx().
*   Parameters : None
*   Return      : None
*********************************************************************************************************
*/
void bsp_InitXxx(void)
{
    /* Step 1: Hardware initialization (clock, GPIO, registers) */
    bsp_InitHardXxx();

    /* Step 2: Software variable / default state initialization */
    bsp_InitVarXxx();
}

/*
*********************************************************************************************************
*   Function   : bsp_InitHardXxx
*   Description : Hardware initialization of XXX (enable clock, configure GPIO
*                 and peripheral registers).
*   Parameters : None
*   Return      : None
*********************************************************************************************************
*/
void bsp_InitHardXxx(void)
{
    /* Step 1: Enable peripheral clock */
    /* Step 2: Configure GPIO / peripheral registers */
}

/*
*********************************************************************************************************
*   Function   : bsp_InitVarXxx
*   Description : Software initialization of XXX (initialize variables, set
*                 default parameters and running state).
*   Parameters : None
*   Return      : None
*********************************************************************************************************
*/
void bsp_InitVarXxx(void)
{
    /* Step 1: Initialize software variables and default parameters */
}

/***************************** AnFuLi Electronics www.armfly.com (END OF FILE) *********************************/

```

## bsp_xxx.h

```c
/*
*********************************************************************************************************
*
*   Module Name : XXX driver module
*   File Name   : bsp_xxx.h
*   Version     : V1.0
*   Description : Header file
*
*   Copyright (C), 2026, AnFuLi Electronics www.armfly.com
*
*********************************************************************************************************
*/

#ifndef __BSP_XXX_H
#define __BSP_XXX_H

/* Function declarations exposed to other modules */
void bsp_InitXxx(void);
void bsp_InitHardXxx(void);
void bsp_InitVarXxx(void);

#endif

/***************************** AnFuLi Electronics www.armfly.com (END OF FILE) *********************************/

```

## Style Notes
- File header "star box": one space between `*` and content; border uses 105 `*` characters.
- Function comment block fixed fields: `Function`, `Description`, `Parameters`, `Return`.
- Indentation: 4 spaces; tabs are forbidden (use spaces only, no `\t` characters).
- Comment style: `/* */`, not `//`; **all comments must be in English**.
- End of file: `END OF FILE` line, followed by **one blank line**.
- Encoding: UTF-8 without BOM; convert GB2312 sources before editing (see rules/encoding.md).
