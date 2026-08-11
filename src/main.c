/* ==========================================================================
 * Filename    : main.c
 * Author      : Hongyi Mei
 * Date        : 2026-08-10
 * Description : Entry point. Phase 1 scope is bring-up only: clock tree,
 *               HAL init, and a blinking LD2 to prove the toolchain, the
 *               linker script, and the flash path all work end to end.
 *               Motor, encoder, and CAN layers are added in later phases.
 * ========================================================================== */

#include "stm32f4xx_hal.h"
#include "app_config.h"

/* Nucleo-F446RE user LED */
#define LD2_PORT   GPIOA
#define LD2_PIN    GPIO_PIN_5

static void SystemClock_Config(void);
static void GPIO_Init(void);
static void Error_Handler(void);

/* -------------------------------------------------------------------------- */
int main(void)
{
    HAL_Init();
    SystemClock_Config();
    GPIO_Init();

    for (;;) {
        HAL_GPIO_TogglePin(LD2_PORT, LD2_PIN);
        HAL_Delay(500);
    }
}

/* --------------------------------------------------------------------------
 * 8 MHz MCO from the on-board ST-LINK -> PLL -> 180 MHz SYSCLK.
 * AHB /1 = 180 MHz, APB1 /4 = 45 MHz (timers 90 MHz), APB2 /2 = 90 MHz.
 * -------------------------------------------------------------------------- */
static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    osc.OscillatorType       = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState             = RCC_HSE_BYPASS;   /* ST-LINK MCO, not a crystal */
    osc.PLL.PLLState         = RCC_PLL_ON;
    osc.PLL.PLLSource        = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM             = 4;    /* 8 MHz  / 4  = 2 MHz  VCO input  */
    osc.PLL.PLLN             = 180;  /* 2 MHz  *180 = 360 MHz VCO out   */
    osc.PLL.PLLP             = RCC_PLLP_DIV2;    /* 360/2 = 180 MHz     */
    osc.PLL.PLLQ             = 7;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        Error_Handler();
    }

    if (HAL_PWREx_EnableOverDrive() != HAL_OK) {  /* required above 168 MHz */
        Error_Handler();
    }

    clk.ClockType      = RCC_CLOCKTYPE_HCLK   | RCC_CLOCKTYPE_SYSCLK |
                         RCC_CLOCKTYPE_PCLK1  | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV4;
    clk.APB2CLKDivider = RCC_HCLK_DIV2;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5) != HAL_OK) {
        Error_Handler();
    }
}

static void GPIO_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();

    gpio.Pin   = LD2_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LD2_PORT, &gpio);
}

static void Error_Handler(void)
{
    __disable_irq();
    for (;;) {
        /* Trap here; attach the debugger and read the call stack. */
    }
}
