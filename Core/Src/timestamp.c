#include "timestamp.h"

#include "main.h"

extern TIM_HandleTypeDef htim2;

void Timestamp_Init(void)
{
  TIM_OC_InitTypeDef oc_cfg = {0};
  uint32_t now = __HAL_TIM_GET_COUNTER(&htim2);

  oc_cfg.OCMode = TIM_OCMODE_TOGGLE;
  oc_cfg.Pulse = now + 1000U;
  oc_cfg.OCPolarity = TIM_OCPOLARITY_HIGH;
  oc_cfg.OCFastMode = TIM_OCFAST_DISABLE;
  (void)HAL_TIM_OC_ConfigChannel(&htim2, &oc_cfg, TIM_CHANNEL_3);

  (void)HAL_TIM_Base_Start(&htim2);
  (void)HAL_TIM_OC_Start(&htim2, TIM_CHANNEL_3);
  (void)HAL_TIM_OC_Start_IT(&htim2, TIM_CHANNEL_3);
}

uint32_t Timestamp_Get(void)
{
  return __HAL_TIM_GET_COUNTER(&htim2);
}
