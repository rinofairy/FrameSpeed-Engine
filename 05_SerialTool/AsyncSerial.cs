// UART I/O belongs to one background worker; the UI never calls the driver.
using System;
using System.Collections.Generic;
using System.IO.Ports;
using System.Text;
using System.Threading;

interface ISerialChannel : IDisposable {
 void Open(); int Available { get; } int Read(byte[] buffer,int length);
 void Write(string value); void Close();
}
sealed class SerialChannel : ISerialChannel {
 readonly SerialPort port;
 public SerialChannel(string name){
  port=new SerialPort(name,115200,Parity.None,8,StopBits.One);
  port.Handshake=Handshake.None;port.DtrEnable=false;port.RtsEnable=false;
  port.ReadTimeout=50;port.WriteTimeout=200;
 }
 public void Open(){port.Open();}
 public int Available {get{return port.BytesToRead;}}
 public int Read(byte[] buffer,int length){return port.Read(buffer,0,length);}
 public void Write(string value){port.Write(value);}
 public void Close(){port.Close();}
 public void Dispose(){port.Dispose();}
}
sealed class AsyncSerial {
 public enum LinkState { Disconnected, Connecting, Connected, Closing, Failed }
 readonly object gate=new object();
 readonly Queue<string> tx=new Queue<string>();
 readonly StringBuilder rx=new StringBuilder();
 readonly Func<string,ISerialChannel> factory;
 Thread worker; volatile bool cancel; LinkState state; string message="未连接";
 DateTime started; bool overflow;
 public AsyncSerial():this(delegate(string name){return new SerialChannel(name);}){}
 public AsyncSerial(Func<string,ISerialChannel> create){factory=create;}
 public LinkState State {get{lock(gate)return state;}}
 public string Message {get{lock(gate)return message;}}
 public bool Busy {get{lock(gate)return worker!=null&&worker.IsAlive;}}
 public bool Start(string name){
  lock(gate){
   if(worker!=null&&worker.IsAlive)return false;
   cancel=false;tx.Clear();rx.Length=0;overflow=false;started=DateTime.UtcNow;
   state=LinkState.Connecting;message="正在连接 "+name+"…";
   worker=new Thread(delegate(){Run(name);});worker.IsBackground=true;
   worker.Name="FrameSpeed UART";worker.Start();return true;
  }
 }
 public void Stop(){
  lock(gate){cancel=true;tx.Clear();
   if(worker!=null&&worker.IsAlive){state=LinkState.Closing;message="正在断开…";}
   else{state=LinkState.Disconnected;message="已断开";}
  }
 }
 public bool Send(string value){
  lock(gate){
   if(state!=LinkState.Connected||cancel)return false;
   if(tx.Count>=256){message="发送队列已满，请稍后重试";return false;}
   tx.Enqueue(value);return true;
  }
 }
 public string Drain(){
  lock(gate){string data=(overflow?"\r\n[日志过多，已省略较早内容]\r\n":"")+rx.ToString();rx.Length=0;overflow=false;return data;}
 }
 // Called by the window timer; only changes flags, never touches SerialPort.
 public void CheckTimeout(){
  lock(gate){if(state==LinkState.Connecting&&(DateTime.UtcNow-started).TotalSeconds>10){
   cancel=true;state=LinkState.Closing;message="连接超时，正在取消；请确认端口和 USB 连接";
  }}
 }
 void Run(string name){
  ISerialChannel channel=null;string error=null;bool opened=false;
  try{
   channel=factory(name);channel.Open();opened=true;
   lock(gate){if(cancel)return;state=LinkState.Connected;message=name+" / 115200 / 8N1；点击窗口后操作";}
   byte[] buffer=new byte[4096];
   while(!cancel){
    // Bound each pass, so incoming logs cannot starve input or cancellation.
    for(int i=0;i<8&&!cancel;i++){
     string value=null;lock(gate){if(tx.Count>0)value=tx.Dequeue();}
     if(value==null)break;channel.Write(value);
    }
    int available=channel.Available;
    if(available>0){
     int n=channel.Read(buffer,Math.Min(available,buffer.Length));
     if(n>0)lock(gate){
      rx.Append(Encoding.ASCII.GetString(buffer,0,n));
      if(rx.Length>65536){rx.Remove(0,rx.Length-65536);overflow=true;}
     }
    }else Thread.Sleep(5);
   }
  }catch(Exception e){if(!cancel)error=e.Message;}
  finally{
   // Closing can also block inside a faulty driver; it stays on this worker.
   if(channel!=null){if(opened)try{channel.Write("@00");}catch{}try{channel.Close();}catch(Exception e){if(error==null&&!cancel)error=e.Message;}try{channel.Dispose();}catch{}}
   lock(gate){tx.Clear();state=error==null?LinkState.Disconnected:LinkState.Failed;
    message=error==null?"已断开":"连接失败："+error;}
  }
 }
}
