using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Text;
using System.Threading;
using System.Windows.Forms;

static class SerialToolTests {
 sealed class FakeChannel : ISerialChannel {
  public readonly ManualResetEvent release=new ManualResetEvent(true);
  public bool failOpen,flood,failRead;public volatile bool closed;
  public readonly List<string> sent=new List<string>();
  public void Open(){release.WaitOne();if(failOpen)throw new IOException("测试：端口被占用");}
  public int Available {get{if(failRead)throw new IOException("测试：USB 断开");return flood?4096:0;}}
  public int Read(byte[] b,int length){for(int i=0;i<length;i++)b[i]=(byte)'x';return length;}
  public void Write(string s){lock(sent)sent.Add(s);}
  public void Close(){closed=true;}
  public void Dispose(){}
 }
 static void Require(bool pass,string message){if(!pass)throw new Exception(message);}
 static void Pump(int ms){var sw=Stopwatch.StartNew();while(sw.ElapsedMilliseconds<ms){Application.DoEvents();Thread.Sleep(5);}}
 static void Wait(Func<bool> condition,int ms,string reason){var sw=Stopwatch.StartNew();while(!condition()&&sw.ElapsedMilliseconds<ms)Pump(10);Require(condition(),reason);}
 static void Transport(){
  var slow=new FakeChannel();slow.release.Reset();var link=new AsyncSerial(delegate(string p){return slow;});
  int heartbeat=0;using(var timer=new System.Windows.Forms.Timer()){
   timer.Interval=15;timer.Tick+=delegate{heartbeat++;};timer.Start();
   var sw=Stopwatch.StartNew();Require(link.Start("TEST"),"start failed");Require(sw.ElapsedMilliseconds<200,"opening blocked caller");
   Pump(350);Require(heartbeat>=8,"window message loop stalled during open");Require(link.State==AsyncSerial.LinkState.Connecting,"slow open state");
   sw.Restart();link.Stop();Require(sw.ElapsedMilliseconds<200,"cancel blocked caller");Require(link.State==AsyncSerial.LinkState.Closing,"cancel state");
   slow.release.Set();Wait(delegate{return !link.Busy;},2000,"cancel cleanup timeout");
   Require(link.State==AsyncSerial.LinkState.Disconnected&&slow.closed,"cancelled connection resurrected");
  }
  var occupied=new FakeChannel();occupied.failOpen=true;link=new AsyncSerial(delegate(string p){return occupied;});link.Start("TEST");
  Wait(delegate{return !link.Busy;},2000,"failed open did not terminate");Require(link.State==AsyncSerial.LinkState.Failed,"occupied port not reported");
  var ready=new FakeChannel();link=new AsyncSerial(delegate(string p){return ready;});link.Start("TEST");
  Wait(delegate{return link.State==AsyncSerial.LinkState.Connected;},2000,"connect timeout");
  link.Send("e");link.Send("1");link.Send("@35");
  Wait(delegate{lock(ready.sent)return ready.sent.Count>=3;},2000,"queued send timeout");
  lock(ready.sent)Require(ready.sent[0]=="e"&&ready.sent[1]=="1"&&ready.sent[2]=="@35","command order changed");
  ready.flood=true;Pump(150);Require(link.Drain().Length<66000,"incoming log not bounded");ready.flood=false;
  ready.failRead=true;Wait(delegate{return !link.Busy;},2000,"unplug cleanup timeout");Require(link.State==AsyncSerial.LinkState.Failed,"unplug not reported");
 }
 static string Capture(AsyncSerial link,int ms){var data=new StringBuilder();var sw=Stopwatch.StartNew();while(sw.ElapsedMilliseconds<ms){data.Append(link.Drain());Pump(20);}data.Append(link.Drain());return data.ToString();}
 static void Smoke(string port,string output){
  var link=new AsyncSerial();var log=new StringBuilder();bool passed=false;string error="";
  try{
   link.Start(port);Wait(delegate{return link.State==AsyncSerial.LinkState.Connected||link.State==AsyncSerial.LinkState.Failed;},12000,"connection timeout");
   Require(link.State==AsyncSerial.LinkState.Connected,link.Message);
   link.Send("m");log.Append(Capture(link,500));link.Send("e");log.Append(Capture(link,600));link.Send("1");log.Append(Capture(link,2500));
   Require(log.ToString().Contains("DEMO_PERF"),"board telemetry missing");
   Require(!log.ToString().Contains("DISPLAY_FAIL"),"board self-test failure");
   link.Send("m");string home=Capture(link,600);log.Append(home);Require(home.Contains("APP_MODE mode=MENU"),"menu response missing");
   passed=true;
  }catch(Exception e){error=e.Message;}
  finally{link.Stop();try{Wait(delegate{return !link.Busy;},2500,"close timeout");}catch(Exception e){passed=false;error=e.Message;}}
  File.WriteAllText(Path.ChangeExtension(output,"log"),log.ToString(),Encoding.UTF8);
  File.WriteAllText(output,"{\"passed\":"+(passed?"true":"false")+",\"port\":\""+port+"\",\"flash_written\":false,\"board_reset\":false,\"error\":\""+error.Replace("\\","\\\\").Replace("\"","\\\"").Replace("\r"," ").Replace("\n"," ")+"\"}",Encoding.UTF8);
  Require(passed,error);
 }
 public static bool Run(string[] args){
  string output=args[args.Length-1];
  try{
   if(args[0]=="--serial-smoke"){Require(args.Length==3,"usage: --serial-smoke COM6 result.json");Smoke(args[1],output);}
   else{Transport();File.WriteAllText(output,"{\"passed\":true,\"scope\":\"slow open / UI message pump / cancellation / occupied port / send ordering / bounded logs / unplug\"}");}
   return true;
  }catch(Exception e){if(args[0]!="--serial-smoke")File.WriteAllText(output,"{\"passed\":false,\"error\":\""+e.Message.Replace("\"","'")+"\"}");return false;}
 }
}
