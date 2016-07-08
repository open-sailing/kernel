#ifndef HISI_REG_H__
#define HISI_REG_H__

#define F(v)    v

/* register definition */
#define DE_STATE1                                        0x100054
#define DE_STATE1_DE_ABORT                               F(0:0)
#define DE_STATE1_DE_ABORT_OFF                           0
#define DE_STATE1_DE_ABORT_ON                            1

#define DE_STATE2                                        0x100058
#define DE_STATE2_DE_FIFO                                F(3:3)
#define DE_STATE2_DE_FIFO_NOTEMPTY                       0
#define DE_STATE2_DE_FIFO_EMPTY                          1
#define DE_STATE2_DE_STATUS                              F(2:2)
#define DE_STATE2_DE_STATUS_IDLE                         0
#define DE_STATE2_DE_STATUS_BUSY                         1
#define DE_STATE2_DE_MEM_FIFO                            F(1:1)
#define DE_STATE2_DE_MEM_FIFO_NOTEMPTY                   0
#define DE_STATE2_DE_MEM_FIFO_EMPTY                      1

#define MISC_CTRL                                     0x000004
#define MISC_CTRL_DAC_POWER                           F(20:20)
#define MISC_CTRL_DAC_POWER_ON                        0
#define MISC_CTRL_DAC_POWER_OFF                       1
#define MISC_CTRL_LOCALMEM_RESET                      F(6:6)
#define MISC_CTRL_LOCALMEM_RESET_RESET                0
#define MISC_CTRL_LOCALMEM_RESET_NORMAL               1

#define CURRENT_GATE                                  0x000040
#define CURRENT_GATE_MCLK                             F(15:14)
#define CURRENT_GATE_CSC                              F(4:4)
#define CURRENT_GATE_CSC_OFF                          0
#define CURRENT_GATE_CSC_ON                           1
#define CURRENT_GATE_DE                               F(3:3)
#define CURRENT_GATE_DE_OFF                           0
#define CURRENT_GATE_DE_ON                            1
#define CURRENT_GATE_DISPLAY                          F(2:2)
#define CURRENT_GATE_DISPLAY_OFF                      0
#define CURRENT_GATE_DISPLAY_ON                       1
#define CURRENT_GATE_LOCALMEM                         F(1:1)
#define CURRENT_GATE_LOCALMEM_OFF                     0
#define CURRENT_GATE_LOCALMEM_ON                      1
#define CURRENT_GATE_DMA                              F(0:0)
#define CURRENT_GATE_DMA_OFF                          0
#define CURRENT_GATE_DMA_ON                           1

#define MODE0_GATE                                    0x000044

#define MODE1_GATE                                    0x000048

#define POWER_MODE_CTRL                               0x00004C

#define POWER_MODE_CTRL_OSC_INPUT                     F(3:3)
#define POWER_MODE_CTRL_OSC_INPUT_OFF                 0
#define POWER_MODE_CTRL_OSC_INPUT_ON                  1
#define POWER_MODE_CTRL_ACPI                          F(2:2)
#define POWER_MODE_CTRL_ACPI_OFF                      0
#define POWER_MODE_CTRL_ACPI_ON                       1
#define POWER_MODE_CTRL_MODE                          F(1:0)
#define POWER_MODE_CTRL_MODE_MODE0                    0
#define POWER_MODE_CTRL_MODE_MODE1                    1
#define POWER_MODE_CTRL_MODE_SLEEP                    2

#define PANEL_PLL_CTRL                                0x00005C
#define PANEL_PLL_CTRL_BYPASS                         F(18:18)
#define PANEL_PLL_CTRL_BYPASS_OFF                     0
#define PANEL_PLL_CTRL_BYPASS_ON                      1
#define PANEL_PLL_CTRL_POWER                          F(17:17)
#define PANEL_PLL_CTRL_POWER_OFF                      0
#define PANEL_PLL_CTRL_POWER_ON                       1
#define PANEL_PLL_CTRL_INPUT                          F(16:16)
#define PANEL_PLL_CTRL_INPUT_OSC                      0
#define PANEL_PLL_CTRL_INPUT_TESTCLK                  1

#define PANEL_PLL_CTRL_POD                        F(15:14)
#define PANEL_PLL_CTRL_OD                         F(13:12)

#define PANEL_PLL_CTRL_N                              F(11:8)
#define PANEL_PLL_CTRL_M                              F(7:0)

#define CRT_PLL_CTRL                                  0x000060
/* Video Control */

#define VIDEO_DISPLAY_CTRL                              0x080040
#define VIDEO_DISPLAY_CTRL_PLANE                        F(2:2)
#define VIDEO_DISPLAY_CTRL_PLANE_DISABLE                0
#define VIDEO_DISPLAY_CTRL_PLANE_ENABLE                 1

/* Video Alpha Control */

#define VIDEO_ALPHA_DISPLAY_CTRL                        0x080080
#define VIDEO_ALPHA_DISPLAY_CTRL_PLANE                  F(2:2)
#define VIDEO_ALPHA_DISPLAY_CTRL_PLANE_DISABLE          0
#define VIDEO_ALPHA_DISPLAY_CTRL_PLANE_ENABLE           1

/* Panel Cursor Control */
#define ALPHA_DISPLAY_CTRL                            0x080100
#define ALPHA_DISPLAY_CTRL_PLANE                      F(2:2)
#define ALPHA_DISPLAY_CTRL_PLANE_DISABLE              0
#define ALPHA_DISPLAY_CTRL_PLANE_ENABLE               1

/* CRT Graphics Control */
#define CRT_DISPLAY_CTRL                              0x080200
#define CRT_DISPLAY_CTRL_RESERVED_1_MASK              F(31:27)
#define CRT_DISPLAY_CTRL_RESERVED_1_MASK_DISABLE      0
#define CRT_DISPLAY_CTRL_RESERVED_1_MASK_ENABLE       0x1F

/* register definition */
#define CRT_DISPLAY_CTRL_DPMS                         F(31:30)
#define CRT_DISPLAY_CTRL_DPMS_0                       0
#define CRT_DISPLAY_CTRL_DPMS_1                       1
#define CRT_DISPLAY_CTRL_DPMS_2                       2
#define CRT_DISPLAY_CTRL_DPMS_3                       3

/* register definition */
#define CRT_DISPLAY_CTRL_CRTSELECT                    F(25:25)
#define CRT_DISPLAY_CTRL_CRTSELECT_VGA                0
#define CRT_DISPLAY_CTRL_CRTSELECT_CRT                1

#define CRT_DISPLAY_CTRL_CLOCK_PHASE                  F(14:14)
#define CRT_DISPLAY_CTRL_CLOCK_PHASE_ACTIVE_HIGH      0
#define CRT_DISPLAY_CTRL_CLOCK_PHASE_ACTIVE_LOW       1
#define CRT_DISPLAY_CTRL_VSYNC_PHASE                  F(13:13)
#define CRT_DISPLAY_CTRL_VSYNC_PHASE_ACTIVE_HIGH      0
#define CRT_DISPLAY_CTRL_VSYNC_PHASE_ACTIVE_LOW       1
#define CRT_DISPLAY_CTRL_HSYNC_PHASE                  F(12:12)
#define CRT_DISPLAY_CTRL_HSYNC_PHASE_ACTIVE_HIGH      0
#define CRT_DISPLAY_CTRL_HSYNC_PHASE_ACTIVE_LOW       1
#define CRT_DISPLAY_CTRL_BLANK                        F(10:10)
#define CRT_DISPLAY_CTRL_BLANK_OFF                    0
#define CRT_DISPLAY_CTRL_BLANK_ON                     1
#define CRT_DISPLAY_CTRL_TIMING                       F(8:8)
#define CRT_DISPLAY_CTRL_TIMING_DISABLE               0
#define CRT_DISPLAY_CTRL_TIMING_ENABLE                1
#define CRT_DISPLAY_CTRL_PLANE                        F(2:2)
#define CRT_DISPLAY_CTRL_PLANE_DISABLE                0
#define CRT_DISPLAY_CTRL_PLANE_ENABLE                 1
#define CRT_DISPLAY_CTRL_FORMAT                       F(1:0)
#define CRT_DISPLAY_CTRL_FORMAT_8                     0
#define CRT_DISPLAY_CTRL_FORMAT_16                    1
#define CRT_DISPLAY_CTRL_FORMAT_32                    2
#define CRT_DISPLAY_CTRL_RESERVED_BITS_MASK           0xFF000200

#define CRT_FB_ADDRESS                                0x080204
#define CRT_FB_ADDRESS_STATUS                         F(31:31)
#define CRT_FB_ADDRESS_STATUS_CURRENT                 0
#define CRT_FB_ADDRESS_STATUS_PENDING                 1
#define CRT_FB_ADDRESS_EXT                            F(27:27)
#define CRT_FB_ADDRESS_EXT_LOCAL                      0
#define CRT_FB_ADDRESS_EXT_EXTERNAL                   1
#define CRT_FB_ADDRESS_ADDRESS                        F(25:0)

#define CRT_FB_WIDTH                                  0x080208
#define CRT_FB_WIDTH_WIDTH                            F(29:16)
#define CRT_FB_WIDTH_OFFSET                           F(13:0)

#define CRT_HORIZONTAL_TOTAL                          0x08020C
#define CRT_HORIZONTAL_TOTAL_TOTAL                    F(27:16)
#define CRT_HORIZONTAL_TOTAL_DISPLAY_END              F(11:0)

#define CRT_HORIZONTAL_SYNC                           0x080210
#define CRT_HORIZONTAL_SYNC_WIDTH                     F(23:16)
#define CRT_HORIZONTAL_SYNC_START                     F(11:0)

#define CRT_VERTICAL_TOTAL                            0x080214
#define CRT_VERTICAL_TOTAL_TOTAL                      F(26:16)
#define CRT_VERTICAL_TOTAL_DISPLAY_END                F(10:0)

#define CRT_VERTICAL_SYNC                             0x080218
#define CRT_VERTICAL_SYNC_HEIGHT                      F(21:16)
#define CRT_VERTICAL_SYNC_START                       F(10:0)

/* Auto Centering */
#define CRT_AUTO_CENTERING_TL                     0x080280
#define CRT_AUTO_CENTERING_TL_TOP                 F(26:16)
#define CRT_AUTO_CENTERING_TL_LEFT                F(10:0)

#define CRT_AUTO_CENTERING_BR                     0x080284
#define CRT_AUTO_CENTERING_BR_BOTTOM              F(26:16)
#define CRT_AUTO_CENTERING_BR_RIGHT               F(10:0)

/* register to control panel output */
#define DISPLAY_CONTROL_HISILE                    0x80288


/* register and values for PLL control */
#define CRT_PLL1_HS                            0x802a8
#define CRT_PLL1_HS_25MHZ	               0x23d40f02
#define CRT_PLL1_HS_40MHZ	               0x23940801
#define CRT_PLL1_HS_65MHZ	               0x23940d01
#define CRT_PLL1_HS_78MHZ	               0x23540F82
#define CRT_PLL1_HS_74MHZ	               0x23941dc2
#define CRT_PLL1_HS_80MHZ	               0x23941001
#define CRT_PLL1_HS_80MHZ_1152	           0x23540fc2
#define CRT_PLL1_HS_108MHZ	               0x23b41b01
#define CRT_PLL1_HS_162MHZ	               0x23480681
#define CRT_PLL1_HS_148MHZ	               0x23541dc2
#define CRT_PLL1_HS_193MHZ	               0x234807c1

#define CRT_PLL2_HS                         0x802ac
#define CRT_PLL2_HS_25MHZ	               0x206B851E
#define CRT_PLL2_HS_40MHZ	               0x30000000
#define CRT_PLL2_HS_65MHZ	               0x40000000
#define CRT_PLL2_HS_78MHZ	               0x50E147AE
#define CRT_PLL2_HS_74MHZ	               0x602B6AE7
#define CRT_PLL2_HS_80MHZ	               0x70000000
#define CRT_PLL2_HS_108MHZ	               0x80000000
#define CRT_PLL2_HS_162MHZ	               0xA0000000
#define CRT_PLL2_HS_148MHZ	               0xB0CCCCCD
#define CRT_PLL2_HS_193MHZ	               0xC0872B02

/* Palette RAM */

/* Panel Palette register starts at 0x080400 ~ 0x0807FC */
#define PANEL_PALETTE_RAM                             0x080400

/* Panel Palette register starts at 0x080C00 ~ 0x080FFC */
#define CRT_PALETTE_RAM                               0x080C00

#define DMA_ABORT_INTERRUPT                             0x0D0020
#define DMA_ABORT_INTERRUPT_ABORT_1                     F(5:5)
#define DMA_ABORT_INTERRUPT_ABORT_1_ENABLE              0
#define DMA_ABORT_INTERRUPT_ABORT_1_ABORT               1

/* cursor control */
#define HWC_ADDRESS                         0x0
#define HWC_ADDRESS_ENABLE                  F(31:31)
#define HWC_ADDRESS_ENABLE_DISABLE          0
#define HWC_ADDRESS_ENABLE_ENABLE           1
#define HWC_ADDRESS_EXT                     F(27:27)
#define HWC_ADDRESS_EXT_LOCAL               0
#define HWC_ADDRESS_EXT_EXTERNAL            1
#define HWC_ADDRESS_CS                      F(26:26)
#define HWC_ADDRESS_CS_0                    0
#define HWC_ADDRESS_CS_1                    1
#define HWC_ADDRESS_ADDRESS                 F(25:0)

#define HWC_LOCATION                        0x4
#define HWC_LOCATION_Y                      F(26:16)
#define HWC_LOCATION_LEFT                   F(11:11)
#define HWC_LOCATION_LEFT_INSIDE            0
#define HWC_LOCATION_LEFT_OUTSIDE           1
#define HWC_LOCATION_X                      F(10:0)

#define HWC_COLOR_12                        0x8

#define HWC_COLOR_3                         0xC

/* accelate 2d graphic */
#define DE_SOURCE                                       0x0
#define DE_SOURCE_WRAP                                  F(31:31)
#define DE_SOURCE_WRAP_DISABLE                          0
#define DE_SOURCE_WRAP_ENABLE                           1
#define DE_SOURCE_X_K1                                  F(29:16)
#define DE_SOURCE_Y_K2                                  F(15:0)
#define DE_SOURCE_X_K1_MONO                             F(20:16)

#define DE_DESTINATION                                  0x4
#define DE_DESTINATION_WRAP                             F(31:31)
#define DE_DESTINATION_WRAP_DISABLE                     0
#define DE_DESTINATION_WRAP_ENABLE                      1
#define DE_DESTINATION_X                                F(28:16)
#define DE_DESTINATION_Y                                F(15:0)

#define DE_DIMENSION                                    0x8
#define DE_DIMENSION_X                                  F(28:16)
#define DE_DIMENSION_Y_ET                               F(15:0)

#define DE_CONTROL                                      0xC
#define DE_CONTROL_STATUS                               F(31:31)
#define DE_CONTROL_STATUS_STOP                          0
#define DE_CONTROL_STATUS_START                         1
#define DE_CONTROL_PATTERN                              F(30:30)
#define DE_CONTROL_PATTERN_MONO                         0
#define DE_CONTROL_PATTERN_COLOR                        1
#define DE_CONTROL_UPDATE_DESTINATION_X                 F(29:29)
#define DE_CONTROL_UPDATE_DESTINATION_X_DISABLE         0
#define DE_CONTROL_UPDATE_DESTINATION_X_ENABLE          1
#define DE_CONTROL_QUICK_START                          F(28:28)
#define DE_CONTROL_QUICK_START_DISABLE                  0
#define DE_CONTROL_QUICK_START_ENABLE                   1
#define DE_CONTROL_DIRECTION                            F(27:27)
#define DE_CONTROL_DIRECTION_LEFT_TO_RIGHT              0
#define DE_CONTROL_DIRECTION_RIGHT_TO_LEFT              1
#define DE_CONTROL_MAJOR                                F(26:26)
#define DE_CONTROL_MAJOR_X                              0
#define DE_CONTROL_MAJOR_Y                              1
#define DE_CONTROL_STEP_X                               F(25:25)
#define DE_CONTROL_STEP_X_POSITIVE                      1
#define DE_CONTROL_STEP_X_NEGATIVE                      0
#define DE_CONTROL_STEP_Y                               F(24:24)
#define DE_CONTROL_STEP_Y_POSITIVE                      1
#define DE_CONTROL_STEP_Y_NEGATIVE                      0
#define DE_CONTROL_STRETCH                              F(23:23)
#define DE_CONTROL_STRETCH_DISABLE                      0
#define DE_CONTROL_STRETCH_ENABLE                       1
#define DE_CONTROL_HOST                                 F(22:22)
#define DE_CONTROL_HOST_COLOR                           0
#define DE_CONTROL_HOST_MONO                            1
#define DE_CONTROL_LAST_PIXEL                           F(21:21)
#define DE_CONTROL_LAST_PIXEL_OFF                       0
#define DE_CONTROL_LAST_PIXEL_ON                        1
#define DE_CONTROL_COMMAND                              F(20:16)
#define DE_CONTROL_COMMAND_BITBLT                       0
#define DE_CONTROL_COMMAND_RECTANGLE_FILL               1
#define DE_CONTROL_COMMAND_DE_TILE                      2
#define DE_CONTROL_COMMAND_TRAPEZOID_FILL               3
#define DE_CONTROL_COMMAND_ALPHA_BLEND                  4
#define DE_CONTROL_COMMAND_RLE_STRIP                    5
#define DE_CONTROL_COMMAND_SHORT_STROKE                 6
#define DE_CONTROL_COMMAND_LINE_DRAW                    7
#define DE_CONTROL_COMMAND_HOST_WRITE                   8
#define DE_CONTROL_COMMAND_HOST_READ                    9
#define DE_CONTROL_COMMAND_HOST_WRITE_BOTTOM_UP         10
#define DE_CONTROL_COMMAND_ROTATE                       11
#define DE_CONTROL_COMMAND_FONT                         12
#define DE_CONTROL_COMMAND_TEXTURE_LOAD                 15
#define DE_CONTROL_ROP_SELECT                           F(15:15)
#define DE_CONTROL_ROP_SELECT_ROP3                      0
#define DE_CONTROL_ROP_SELECT_ROP2                      1
#define DE_CONTROL_ROP2_SOURCE                          F(14:14)
#define DE_CONTROL_ROP2_SOURCE_BITMAP                   0
#define DE_CONTROL_ROP2_SOURCE_PATTERN                  1
#define DE_CONTROL_MONO_DATA                            F(13:12)
#define DE_CONTROL_MONO_DATA_NOT_PACKED                 0
#define DE_CONTROL_MONO_DATA_8_PACKED                   1
#define DE_CONTROL_MONO_DATA_16_PACKED                  2
#define DE_CONTROL_MONO_DATA_32_PACKED                  3
#define DE_CONTROL_REPEAT_ROTATE                        F(11:11)
#define DE_CONTROL_REPEAT_ROTATE_DISABLE                0
#define DE_CONTROL_REPEAT_ROTATE_ENABLE                 1
#define DE_CONTROL_TRANSPARENCY_MATCH                   F(10:10)
#define DE_CONTROL_TRANSPARENCY_MATCH_OPAQUE            0
#define DE_CONTROL_TRANSPARENCY_MATCH_TRANSPARENT       1
#define DE_CONTROL_TRANSPARENCY_SELECT                  F(9:9)
#define DE_CONTROL_TRANSPARENCY_SELECT_SOURCE           0
#define DE_CONTROL_TRANSPARENCY_SELECT_DESTINATION      1
#define DE_CONTROL_TRANSPARENCY                         F(8:8)
#define DE_CONTROL_TRANSPARENCY_DISABLE                 0
#define DE_CONTROL_TRANSPARENCY_ENABLE                  1
#define DE_CONTROL_ROP                                  F(7:0)

/* Pseudo fields. */

#define DE_CONTROL_SHORT_STROKE_DIR                     F(27:24)
#define DE_CONTROL_SHORT_STROKE_DIR_225                 0
#define DE_CONTROL_SHORT_STROKE_DIR_135                 1
#define DE_CONTROL_SHORT_STROKE_DIR_315                 2
#define DE_CONTROL_SHORT_STROKE_DIR_45                  3
#define DE_CONTROL_SHORT_STROKE_DIR_270                 4
#define DE_CONTROL_SHORT_STROKE_DIR_90                  5
#define DE_CONTROL_SHORT_STROKE_DIR_180                 8
#define DE_CONTROL_SHORT_STROKE_DIR_0                   10
#define DE_CONTROL_ROTATION                             F(25:24)
#define DE_CONTROL_ROTATION_0                           0
#define DE_CONTROL_ROTATION_270                         1
#define DE_CONTROL_ROTATION_90                          2
#define DE_CONTROL_ROTATION_180                         3

#define DE_PITCH                                        0x000010
#define DE_PITCH_DESTINATION                            F(28:16)
#define DE_PITCH_SOURCE                                 F(12:0)

#define DE_FOREGROUND                                   0x000014

#define DE_BACKGROUND                                   0x000018

#define DE_STRETCH_FORMAT                               0x00001C
#define DE_STRETCH_FORMAT_PATTERN_XY                    F(30:30)
#define DE_STRETCH_FORMAT_PATTERN_XY_NORMAL             0
#define DE_STRETCH_FORMAT_PATTERN_XY_OVERWRITE          1
#define DE_STRETCH_FORMAT_PATTERN_Y                     F(29:27)
#define DE_STRETCH_FORMAT_PATTERN_X                     F(25:23)
#define DE_STRETCH_FORMAT_PIXEL_FORMAT                  F(21:20)
#define DE_STRETCH_FORMAT_PIXEL_FORMAT_8                0
#define DE_STRETCH_FORMAT_PIXEL_FORMAT_16               1
#define DE_STRETCH_FORMAT_PIXEL_FORMAT_32               2
#define DE_STRETCH_FORMAT_PIXEL_FORMAT_24               3

#define DE_STRETCH_FORMAT_ADDRESSING                    F(19:16)
#define DE_STRETCH_FORMAT_ADDRESSING_XY                 0
#define DE_STRETCH_FORMAT_ADDRESSING_LINEAR             15
#define DE_STRETCH_FORMAT_SOURCE_HEIGHT                 F(11:0)

#define DE_COLOR_COMPARE                                0x000020

#define DE_COLOR_COMPARE_MASK                           0x000024

#define DE_MASKS                                        0x000028

#define DE_CLIP_TL                                      0x00002C

#define DE_CLIP_BR                                      0x000030

#define DE_WINDOW_WIDTH                                 0x00003C
#define DE_WINDOW_WIDTH_DESTINATION                     F(28:16)
#define DE_WINDOW_WIDTH_SOURCE                          F(12:0)

#define DE_WINDOW_SOURCE_BASE                           0x000040
#define DE_WINDOW_SOURCE_BASE_EXT                       F(27:27)
#define DE_WINDOW_SOURCE_BASE_EXT_LOCAL                 0
#define DE_WINDOW_SOURCE_BASE_EXT_EXTERNAL              1
#define DE_WINDOW_SOURCE_BASE_CS                        F(26:26)
#define DE_WINDOW_SOURCE_BASE_CS_0                      0
#define DE_WINDOW_SOURCE_BASE_CS_1                      1
#define DE_WINDOW_SOURCE_BASE_ADDRESS                   F(25:0)

#define DE_WINDOW_DESTINATION_BASE                      0x000044
#define DE_WINDOW_DESTINATION_BASE_EXT                  F(27:27)
#define DE_WINDOW_DESTINATION_BASE_EXT_LOCAL            0
#define DE_WINDOW_DESTINATION_BASE_EXT_EXTERNAL         1
#define DE_WINDOW_DESTINATION_BASE_CS                   F(26:26)
#define DE_WINDOW_DESTINATION_BASE_CS_0                 0
#define DE_WINDOW_DESTINATION_BASE_CS_1                 1
#define DE_WINDOW_DESTINATION_BASE_ADDRESS              F(25:0)
#endif
