// AI-assisted project utility. UART controls/logging only; never JTAG or Flash.
using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Ports;
using System.Text;
using System.Windows.Forms;
using System.Drawing;
class FrameSpeedSerial : Form {
 readonly ComboBox ports=new ComboBox(), role=new ComboBox();
 readonly Button connect=new Button();readonly TextBox output=new TextBox();
 readonly Label status=new Label();readonly Timer timer=new Timer();
 readonly HashSet<Keys> held=new HashSet<Keys>();readonly StringBuilder history=new StringBuilder();
 readonly AsyncSerial serial=new AsyncSerial();int ticks,displayTicks;string incoming="";
 readonly StringBuilder pendingDisplay=new StringBuilder();
 static uint Mask(HashSet<Keys> keys){uint m=0;if(keys.Contains(Keys.W))m|=1;if(keys.Contains(Keys.S))m|=2;if(keys.Contains(Keys.A))m|=4;if(keys.Contains(Keys.D))m|=8;if(keys.Contains(Keys.J))m|=16;if(keys.Contains(Keys.ShiftKey)||keys.Contains(Keys.LShiftKey)||keys.Contains(Keys.RShiftKey))m|=32;return m;}
 bool Game {get{return role.SelectedIndex==2;}}
 static int Mode(string line){return line.StartsWith("APP_MODE mode=MENU")?0:(line.StartsWith("APP_MODE mode=DEMO")||line.StartsWith("DEMO_PERF"))?1:(line.StartsWith("APP_MODE mode=GAME")||line.StartsWith("GAME_PERF"))?2:-1;}
 bool Ready {get{return serial.State==AsyncSerial.LinkState.Connected;}}
 void Send(string s){serial.Send(s);}
 void ClosePort(){held.Clear();serial.Stop();connect.Text="正在断开";}
 void RefreshPorts(){ports.Items.Clear();foreach(string p in SerialPort.GetPortNames())ports.Items.Add(p);if(ports.Items.Contains("COM6"))ports.SelectedItem="COM6";else if(ports.Items.Count>0)ports.SelectedIndex=0;}
 void Command(string c,bool demoOnly){if(!demoOnly||role.SelectedIndex==1)Send(c);}
 FrameSpeedSerial(){
  Text="帧速引擎 Demo + Taisei Stage1 交付版";Size=new Size(980,620);MinimumSize=new Size(760,480);KeyPreview=true;
  var top=new FlowLayoutPanel{Dock=DockStyle.Top,Height=138,Padding=new Padding(8),WrapContents=true};Controls.Add(top);
  ports.DropDownStyle=ComboBoxStyle.DropDownList;ports.Width=90;top.Controls.Add(ports);
  var refresh=new Button{Text="刷新串口"};refresh.Click+=delegate{if(!serial.Busy)RefreshPorts();};top.Controls.Add(refresh);
  role.DropDownStyle=ComboBoxStyle.DropDownList;role.Items.AddRange(new object[]{"开始菜单","性能 Demo","Taisei 游戏"});role.SelectedIndex=0;role.Width=130;role.SelectedIndexChanged+=delegate{if(Ready)Send("@00");held.Clear();};top.Controls.Add(role);
  connect.Text="连接";connect.Click+=delegate{
   if(serial.Busy){ClosePort();return;}
   if(ports.SelectedItem==null){status.Text="没有可用串口";return;}
   incoming="";pendingDisplay.Length=0;held.Clear();
   if(serial.Start(ports.SelectedItem.ToString())){connect.Text="取消连接";status.Text="正在连接…";}
  };top.Controls.Add(connect);
  string[] labels={"主菜单 M","进入 Demo E","进入 Taisei G","FPGA H","CPU C","背景 B","开始 R","无敌 I","暂停 P","纯色填充 1","双色搬运 2","精灵压力 3","数量 +32","数量 -32","Alpha A","透明色 K","Alpha -","Alpha +","晶体 4","球体 5","混合 6"};
  string[] commands={"m","e","g","h","c","b","r","i","p","1","2","3","+","-","a","k","[","]","4","5","6"};
  for(int n=0;n<labels.Length;n++){int index=n;var button=new Button{Text=labels[n],AutoSize=true};button.Click+=delegate{Command(commands[index],index>=9);};top.Controls.Add(button);}
  var save=new Button{Text="保存日志",AutoSize=true};save.Click+=delegate{using(var dialog=new SaveFileDialog{Filter="日志|*.log",FileName="framespeed_uart.log"})if(dialog.ShowDialog()==DialogResult.OK)File.WriteAllText(dialog.FileName,history.ToString(),Encoding.UTF8);};top.Controls.Add(save);
  var help=new Label{Dock=DockStyle.Top,Height=44,Padding=new Padding(8),Text="菜单：1 Demo / 2 Taisei；M 返回。Demo：1 填充 / 2 双色搬运 / 3 精灵，4-6 素材，A/K 效果，[] Alpha。Taisei：WASD、J、Shift，R/I/P，T/N 练习，只有 Lunatic。模式随板卡日志自动切换。"};Controls.Add(help);help.BringToFront();top.BringToFront();
  output.Multiline=true;output.ReadOnly=true;output.ScrollBars=ScrollBars.Both;output.WordWrap=false;output.Dock=DockStyle.Fill;output.Font=new Font("Consolas",10);Controls.Add(output);output.BringToFront();help.BringToFront();top.BringToFront();
  status.Dock=DockStyle.Bottom;status.Height=26;status.Text="未连接；请先加载 FPGA 和 RISC-V 程序";Controls.Add(status);status.BringToFront();
  KeyDown+=delegate(object sender,KeyEventArgs e){
   if(!Ready)return;
   if(Game&&(e.KeyCode==Keys.W||e.KeyCode==Keys.A||e.KeyCode==Keys.S||e.KeyCode==Keys.D||e.KeyCode==Keys.J||e.KeyCode==Keys.ShiftKey)){held.Add(e.KeyCode);Send("@"+Mask(held).ToString("X2"));e.SuppressKeyPress=true;return;}
   if(held.Contains(e.KeyCode))return;held.Add(e.KeyCode);
   if(e.KeyCode==Keys.Add||e.KeyCode==Keys.Oemplus)Command("+",true);
   else if(e.KeyCode==Keys.Subtract||e.KeyCode==Keys.OemMinus)Command("-",true);
   else if(e.KeyCode>=Keys.D1&&e.KeyCode<=Keys.D7)Send(((char)('1'+(int)e.KeyCode-(int)Keys.D1)).ToString());
   else if(e.KeyCode==Keys.OemOpenBrackets)Command("[",true);else if(e.KeyCode==Keys.OemCloseBrackets)Command("]",true);
   else if(e.KeyCode>=Keys.A&&e.KeyCode<=Keys.Z)Send(e.KeyCode.ToString().ToLowerInvariant());
   e.SuppressKeyPress=true;
  };
  KeyUp+=delegate(object sender,KeyEventArgs e){held.Remove(e.KeyCode);if(Game&&Ready)Send("@"+Mask(held).ToString("X2"));};
  Deactivate+=delegate{held.Clear();if(Game&&Ready)Send("@00");};FormClosing+=delegate{timer.Stop();ClosePort();};
  timer.Interval=20;timer.Tick+=delegate{
   serial.CheckTimeout();status.Text=serial.Message;
   AsyncSerial.LinkState state=serial.State;
   connect.Text=state==AsyncSerial.LinkState.Connected?"断开":state==AsyncSerial.LinkState.Connecting?"取消连接":state==AsyncSerial.LinkState.Closing?"正在断开":"连接";
   ports.Enabled=!serial.Busy;
   string s=serial.Drain();if(s.Length>0){
    incoming+=s;int nl;while((nl=incoming.IndexOf('\n'))>=0){string line=incoming.Substring(0,nl).Trim();incoming=incoming.Substring(nl+1);int next=Mode(line);if(next>=0)role.SelectedIndex=next;}
    if(incoming.Length>8192)incoming="";history.Append(s);if(history.Length>1000000)history.Remove(0,history.Length-1000000);
    pendingDisplay.Append(s);
   }
   // Render at most ten times per second; keep the window responsive to logs.
   if(++displayTicks%5==0&&pendingDisplay.Length>0){
    output.AppendText(pendingDisplay.ToString());pendingDisplay.Length=0;
    if(output.TextLength>150000)output.Text=output.Text.Substring(output.TextLength-100000);
    output.SelectionStart=output.TextLength;output.ScrollToCaret();
   }
   if(Game&&Ready&&++ticks%3==0)Send("@"+Mask(held).ToString("X2"));
  };RefreshPorts();timer.Start();
 }
 [STAThread] static void Main(string[] args){
  if(args.Length>=2&&(args[0]=="--transport-test"||args[0]=="--serial-smoke")){
   bool passed=SerialToolTests.Run(args);Environment.Exit(passed?0:1);return;
  }
  if(args.Length==2&&args[0]=="--self-test"){
   var k=new HashSet<Keys>{Keys.W,Keys.A,Keys.J,Keys.ShiftKey};bool pass=Mask(k)==53;k.Remove(Keys.ShiftKey);pass&=Mask(k)==21;k.Clear();pass&=Mask(k)==0;pass&=Mode("APP_MODE mode=MENU")==0&&Mode("APP_MODE mode=DEMO")==1&&Mode("APP_MODE mode=GAME")==2&&Mode("DEMO_PERF mode=1")==-1;
   File.WriteAllText(args[1],"{\"passed\":"+(pass?"true":"false")+",\"scope\":\"held-key encoding, Shift release and mode detection; no serial/GUI opened\"}");Environment.Exit(pass?0:1);return;
  }
  Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);Application.Run(new FrameSpeedSerial());
 }
}
