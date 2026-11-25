import { AbilityConstant, UIAbility, Want } from '@kit.AbilityKit';
import { window } from '@kit.ArkUI';
import { DesktopHost } from '../pages/DesktopHost';

export default class EntryAbility extends UIAbility {
  private loginLaunch: boolean = false;
  onCreate(_want: Want, launchParam: AbilityConstant.LaunchParam): void {
    this.loginLaunch = launchParam.launchReason === AbilityConstant.LaunchReason.AUTO_STARTUP;
  }
  onPrepareToTerminate(): boolean { return DesktopHost.requestExit(); }
  onNewWant(_want: Want, launchParam: AbilityConstant.LaunchParam): void {
    if (launchParam.launchReason !== AbilityConstant.LaunchReason.AUTO_STARTUP) {
      this.loginLaunch = false;
      DesktopHost.restore();
    }
  }
  onForeground(): void { DesktopHost.foreground(); }
  onWindowStageCreate(stage: window.WindowStage): void {
    DesktopHost.start(stage, this.context, this.loginLaunch);
  }
}
