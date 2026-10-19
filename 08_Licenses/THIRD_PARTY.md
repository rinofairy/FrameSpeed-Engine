# 来源

Taisei Project：https://github.com/taisei-project/taisei ，固定提交 f4d3342226e915f369db8680688723adb18e4efa。关卡时间线、敌群、道中 Boss、灵梦 A 武器、敌机血量、Power、伤害规则均参考原作源码，原文和 MIT 许可保留在 upstream/taisei。83 个源码／贴图经 Git blob 核对，来源清单在 06_Verification/reference/game/original_rules_audit.json；旧原作素材清单同时保留。

本次新增红妖精、大／巨大／超级妖精、旋涡、米弹、粗米弹、针弹和灵符等原作贴图；通过本地素材预处理脚本（该脚本未包含在此公开版） 缩放、RGB565 转换及离线材质着色。素材与原作规则不是本队原创。角色／Boss 双帧及 HUD 原图来源见旧清单；当前图集只保留自机双帧，以容纳不同敌类，其他角色使用静态帧。

HUD、Cirno 背景取自 v1.2，许可见 COPYING-v1.2.txt，图片缩放、裁剪、压暗后用于板卡显示。定点状态机、裁剪、批量合成、CPU／FPGA 调度、串口工具与相关实现含 AI 协助。

Sapphire 处理器、DDR、接口、时钟与板级工程沿用易灵思 Ti60F225 DemoBoard V4 参考设计，保留厂商版权／保护源码。软件和 IP 使用仍依厂商授权。


公开版排除了厂商专有 IP、BSP、板级参考 RTL 及含这些组件的预编译固件。文档中的完整工程部署说明用于解释本地系统，不构成这些依赖的再分发授权。
