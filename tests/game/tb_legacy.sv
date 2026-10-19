`timescale 1ns/1ps
module tb_blitter;
 reg clk=0;always #5 clk=~clk;
 reg reset=1;
 reg [15:0] paddr=0;reg psel=0,penable=0,pwrite=0;reg [31:0] pwdata=0;
 wire [31:0] prdata;wire pready,pslverror;
 wire [31:0] awaddr,araddr,wdata;wire [7:0] awlen,arlen;
 wire [2:0] awsize,arsize;wire [1:0] awburst,arburst;wire [3:0] wstrb;
 wire awvalid,arvalid,wvalid,wlast,rready,bready;
 wire awready,arready,wready;
 reg [31:0] rdata=0;reg [1:0] rresp=0,bresp=0;reg rlast=0,rvalid=0,bvalid=0;
 fs_blitter_legacy dut(.busy_out(),.*);
 localparam BASE=32'h00400000,SIZE=131072;
 reg [7:0] mem[0:SIZE-1],expected[0:SIZE-1];
 reg reading=0,writing=0,inject_read_error=0,inject_background_error=0;
 reg inject_prefetch_error=0,inject_write_error=0,prefetch_read=0,no_stalls=0;
 integer extra_pf_delay=0,pf_delay=0;
 integer ra,rl,ri,wa,wl,wi,cycle=0,bdelay=0,seed=32'h54327,random_value;
 reg stall_r=0,stall_w=0,stall_a=0;
 assign arready=!reading && !rvalid && !stall_a;
 assign awready=!writing && !bvalid && bdelay==0 && !stall_a;
 assign wready=writing && !stall_w;
 reg blocked_w=0;reg [36:0] held_w;
 reg blocked_ar=0,blocked_aw=0;reg [39:0] held_ar,held_aw;
 integer b;
 always @(posedge clk)begin
  cycle<=cycle+1;
  random_value=$random(seed);stall_a<=!no_stalls && random_value[0];stall_w<=!no_stalls && random_value[2];stall_r<=!no_stalls && random_value[4];
  if(!reset)begin
   if(blocked_w && (!wvalid || {wdata,wstrb,wlast}!==held_w))$fatal(1,"W changed under backpressure");
   blocked_w<=wvalid && !wready;held_w<={wdata,wstrb,wlast};
   if(blocked_ar && (!arvalid || {araddr,arlen}!==held_ar))$fatal(1,"AR changed under backpressure");
   if(blocked_aw && (!awvalid || {awaddr,awlen}!==held_aw))$fatal(1,"AW changed under backpressure");
   blocked_ar<=arvalid&&!arready;held_ar<={araddr,arlen};
   blocked_aw<=awvalid&&!awready;held_aw<={awaddr,awlen};
   if(arvalid && arready)begin
    if(arsize!=2 || arburst!=1 || araddr[1:0]!=0 || araddr[11:0]+(arlen+1)*4>4096)$fatal(1,"Bad AR burst");
    ra<=araddr-BASE;rl<=arlen+1;ri<=0;reading<=1;
    prefetch_read<=dut.pf_state==dut.PF_ADDR;
    pf_delay<=dut.pf_state==dut.PF_ADDR?extra_pf_delay:0;
   end
   if(pf_delay>0)pf_delay<=pf_delay-1;
   if(reading && !rvalid && !stall_r && pf_delay==0)begin
    if(ra+ri*4<0 || ra+ri*4+3>=SIZE)$fatal(1,"Read outside test RAM");
    rdata<={mem[ra+ri*4+3],mem[ra+ri*4+2],mem[ra+ri*4+1],mem[ra+ri*4]};
    rvalid<=1;rlast<=ri==rl-1;rresp<=(inject_read_error || (inject_background_error && ra>=32768) || (inject_prefetch_error && prefetch_read)) ? 2 : 0;
   end
   if(rvalid && rready)begin
    rvalid<=0;ri<=ri+1;if(rlast)reading<=0;
    // Continuous R beats in the deterministic comparison and final cases.
    if(no_stalls && !rlast)begin
     rvalid<=1;rlast<=ri+1==rl-1;
     rdata<={mem[ra+(ri+1)*4+3],mem[ra+(ri+1)*4+2],mem[ra+(ri+1)*4+1],mem[ra+(ri+1)*4]};
    end
   end
   if(awvalid && awready)begin
    if(awsize!=2 || awburst!=1 || awaddr[1:0]!=0 || awaddr[11:0]+(awlen+1)*4>4096)$fatal(1,"Bad AW burst");
    wa<=awaddr-BASE;wl<=awlen+1;wi<=0;writing<=1;
   end
   if(wvalid && wready)begin
    if(wlast!==(wi==wl-1))$fatal(1,"Wrong WLAST");
    if(wa+wi*4<0 || wa+wi*4+3>=SIZE)$fatal(1,"Write outside test RAM");
    for(b=0;b<4;b=b+1)if(wstrb[b])mem[wa+wi*4+b]<=wdata[b*8+:8];
    wi<=wi+1;
    if(wlast)begin writing<=0;bdelay<=3;end
   end
   if(bdelay>0)begin bdelay<=bdelay-1;if(bdelay==1)begin bvalid<=1;bresp<=inject_write_error?2:0;end end
   if(bvalid && bready)bvalid<=0;
  end
 end
 task apb_write(input [15:0] addr,input [31:0] data);
  begin
   @(negedge clk);paddr=addr;pwdata=data;psel=1;pwrite=1;penable=0;
   @(negedge clk);penable=1;
   @(negedge clk);psel=0;pwrite=0;penable=0;
  end
 endtask
 integer sequence_number=0,tests=0;
 task command(input integer op,s,d,ss,ds,w,h,alpha,error_code);
  integer timeout;
  begin
   sequence_number=sequence_number+1;
   apb_write(4,6);apb_write('hc,op);apb_write('h10,BASE+s);apb_write('h14,BASE+d);
   apb_write('h18,ss);apb_write('h1c,ds);apb_write('h20,w);apb_write('h24,h);
   apb_write('h28,'h1234);apb_write('h2c,'hf81f);apb_write('h30,alpha);apb_write('h34,sequence_number);
   apb_write(8,1);timeout=0;
   while(!dut.done && timeout<100000)begin @(negedge clk);timeout=timeout+1;end
   if(timeout>=100000 || dut.errcode!=error_code || dut.complete_seq!=sequence_number)
    $fatal(1,"Command failure seq=%0d err=%0d expected=%0d",sequence_number,dut.errcode,error_code);
   if(reading || rvalid || writing || bvalid || dut.pf_active)$fatal(1,"Done with AXI outstanding");
   tests=tests+1;
  end
 endtask
 function [15:0] blend(input [15:0] fg,bg,input integer a);
  integer red,green,blue;
  begin
   red=(((fg>>11)&31)*a+((bg>>11)&31)*(255-a)+127)/255;
   green=(((fg>>5)&63)*a+((bg>>5)&63)*(255-a)+127)/255;
   blue=((fg&31)*a+(bg&31)*(255-a)+127)/255;
   blend=(red<<11)|(green<<5)|blue;
  end
 endfunction
 task run_case(input integer op,s,d,ss,ds,w,h,a);
  integer i,x,y,sp,dp,bytes_written;reg [15:0] fg,bg,pixel;
  begin
   for(i=0;i<SIZE;i=i+1)expected[i]=mem[i];bytes_written=0;
   for(y=0;y<h;y=y+1)for(x=0;x<w;x=x+1)begin
    sp=s+y*ss+x*2;dp=d+y*ds+x*2;fg={mem[sp+1],mem[sp]};bg={mem[dp+1],mem[dp]};
    pixel=op==0 ? 16'h1234 : op>=3 ? blend(fg,bg,a) : fg;
    if(!((op==2 || op==4) && fg==16'hf81f))begin
     expected[dp]=pixel[7:0];expected[dp+1]=pixel[15:8];bytes_written=bytes_written+2;
    end
   end
   command(op,s,d,ss,ds,w,h,a,0);
   for(i=0;i<SIZE;i=i+1)if(mem[i]!==expected[i])$fatal(1,"Pixel/guard mismatch op=%0d addr=%h actual=%h expected=%h",op,i,mem[i],expected[i]);
   if(dut.write_bytes!=bytes_written)$fatal(1,"Wrong write byte counter");
  end
 endtask
 task preload(input integer s,words,slot,expected_error);
  integer timeout;
  begin
   sequence_number=sequence_number+1;
   apb_write(4,6);apb_write('hc,5);apb_write('h10,BASE+s);apb_write('h20,words);
   apb_write('h24,1);apb_write('h28,slot);apb_write('h34,sequence_number);apb_write(8,1);
   timeout=0;
   while(!dut.done && timeout<100000)begin @(negedge clk);timeout=timeout+1;end
   if(timeout>=100000 || dut.errcode!=expected_error)$fatal(1,"Preload failure");
   tests=tests+1;
  end
 endtask
 integer i,u,v,width_case,hits_before,saved_before,off_cycles,on_cycles;reg [15:0] pixel;
 initial begin
  for(i=0;i<SIZE/2;i=i+1)begin pixel=(i%3==0)?16'hf81f:((i*13)^16'ha55a);mem[2*i]=pixel[7:0];mem[2*i+1]=pixel[15:8];end
  repeat(5)@(negedge clk);reset=0;
  run_case(0,0,32770,0,78,37,5,255);
  run_case(1,4094,36858,78,82,37,5,255);
  run_case(2,4094,32766,78,82,37,5,255);
  run_case(2,4096,32770,64,66,31,4,255);
  run_case(2,4098,32768,66,64,31,4,255);
  run_case(2,4096,32768,64,64,32,4,255);
  run_case(2,4096,32768,2,2,1,3,255);
  for(i=0;i<64;i=i+1)begin mem[4096+i*2]='h1f;mem[4097+i*2]='hf8;end
  run_case(2,4096,32768,64,64,32,2,255);
`ifdef TEST_ALPHA
  for(i=0;i<256;i=i+1)begin pixel=(i%3==0)?16'hf81f:((i*31)^16'h5555);mem[4096+i*2]=pixel[7:0];mem[4097+i*2]=pixel[15:8];end
  run_case(3,4096,32768,64,64,32,4,0);
  run_case(3,4096,32768,64,64,32,4,255);
  run_case(3,4098,32768,66,66,31,4,1);
  run_case(3,4094,36858,78,82,37,5,127);
  run_case(3,4096,32770,64,66,31,4,128);
  run_case(3,4098,32770,64,64,32,4,254);
  run_case(4,4096,32768,64,64,32,4,128);
  run_case(4,4094,36858,78,82,37,5,255);
  for(u=0;u<4;u=u+1)for(v=0;v<7;v=v+1)begin
   case(v)0:width_case=1;1:width_case=31;2:width_case=32;3:width_case=33;
           4:width_case=63;5:width_case=64;default:width_case=65;endcase
   run_case(3,8192+(u&1)*2,49152+((u>>1)&1)*2,134,138,width_case,2,128);
   run_case(4,12286+(u&1)*2,65530+((u>>1)&1)*2,134,138,width_case,2,127);
  end
`endif
  command(9,4096,32768,64,64,32,1,255,1);
  command(2,4097,32768,64,64,32,1,255,2);
  command(2,4096,4096,64,64,32,1,255,4);
  command(2,4096,32768,0,0,0,1,255,0);
  inject_read_error=1;command(2,4096,32768,64,64,32,1,255,5);inject_read_error=0;
  // Background AXI error must discard all pipelined outputs, then recover.
  inject_background_error=1;command(4,4096,32768,64,64,32,2,128,5);inject_background_error=0;
  run_case(4,4096,32768,64,64,32,2,128);
  apb_write('h54,3);
  preload(8192,1024,0,0);preload(12284,1024,1,0); // second slot crosses 4-KiB boundary
  if(dut.cache_valid!=3)$fatal(1,"Cache not valid after preload");
  hits_before=dut.cache_hits;saved_before=dut.cache_saved;
  for(u=0;u<4;u=u+1)for(v=0;v<7;v=v+1)begin
   case(v)0:width_case=1;1:width_case=31;2:width_case=32;3:width_case=33;
           4:width_case=63;5:width_case=64;default:width_case=65;endcase
   run_case(3,8192+(u&1)*2,49152+((u>>1)&1)*2,134,138,width_case,2,128);
   run_case(4,12286+(u&1)*2,65530+((u>>1)&1)*2,134,138,width_case,2,127);
  end
  if(dut.cache_hits<=hits_before || dut.cache_saved<=saved_before)$fatal(1,"No cache hits");
  // Partial coverage: covered rows hit; remaining rows use DDR normally.
  preload(8192,16,0,0);run_case(4,8192,49152,64,128,32,3,254);
  run_case(2,12286,65530,134,138,65,2,255);
  run_case(1,12286,65530,134,138,65,2,255);
  // Accelerator writes must invalidate a source region before the next read.
  run_case(0,0,12288,0,64,32,1,255);
  if(dut.cache_valid[1])$fatal(1,"Write failed to invalidate slot");
  run_case(4,12286,65530,134,138,65,2,127);
  // Failed preload cannot expose partial texture data.
  inject_read_error=1;preload(8192,512,0,5);inject_read_error=0;
  if(dut.cache_valid[0])$fatal(1,"Failed load kept cache valid");
  preload(8192,512,0,0);
  inject_background_error=1;command(4,8192,49152,64,64,32,2,128,5);inject_background_error=0;
  run_case(4,8192,49152,64,64,32,2,128);
  // CPU changes require explicit invalidation and reload.
  apb_write('h54,3);mem[8192]=8'hab;mem[8193]=8'hcd;
  run_case(4,8192,49152,64,64,32,2,128);
  preload(8192,512,0,0);run_case(4,8192,49152,64,64,32,2,128);
  preload(8193,512,0,2);preload(8192,1025,0,2);preload(8192,0,0,2);preload(8192,8,2,2);
  // Prefetched read fault must not leak a bus transaction into the next command.
  inject_prefetch_error=1;command(4,8192,49152,128,128,64,4,128,5);inject_prefetch_error=0;
  run_case(4,8192,49152,128,128,64,4,128);
  inject_prefetch_error=1;command(1,16384,49152,256,256,128,3,255,5);inject_prefetch_error=0;
  run_case(1,16384,49152,256,256,128,3,255);
  // B error arrives before speculative R data; finish must drain that response.
  extra_pf_delay=80;inject_write_error=1;
  command(4,8192,49152,128,128,64,3,128,6);
  inject_write_error=0;extra_pf_delay=0;run_case(4,8192,49152,128,128,64,3,128);
  // Tight odd stride means adjacent rows share a word, but never a live pixel.
  run_case(4,8194,49154,14,14,7,19,128);
  run_case(4,8192,49152,2,2,1,37,254);
  // Same target on successive commands checks ordered, current-frame layering.
  for(u=0;u<4;u=u+1)run_case(4,8192+u*2,49152,134,138,65,3,64+u*40);
  // Deterministic memory timing comparison; no performance claim for real DDR.
  no_stalls=1;apb_write('h64,0);run_case(1,16384,65536,512,512,256,4,255);off_cycles=dut.cycles;
  apb_write('h64,1);run_case(1,16384,65536,512,512,256,4,255);on_cycles=dut.cycles;
  if(on_cycles>=off_cycles)$fatal(1,"No copy pipeline improvement");
  $display("PREFETCH_SIM_COPY off=%0d on=%0d",off_cycles,on_cycles);
  preload(8192,1024,0,0);apb_write('h64,0);run_case(4,8192,49152,128,128,64,16,128);off_cycles=dut.cycles;
  apb_write('h64,1);run_case(4,8192,49152,128,128,64,16,128);on_cycles=dut.cycles;
  if(on_cycles>=off_cycles || !dut.pf_hits || !dut.pf_overlap)$fatal(1,"No alpha overlap");
  $display("PREFETCH_SIM_ALPHA off=%0d on=%0d hits=%0d overlap=%0d",off_cycles,on_cycles,dut.pf_hits,dut.pf_overlap);
  run_case(4,8194,49154,134,138,65,4,127);
  run_case(4,8194,49152,134,138,65,4,128);
  run_case(1,16382,65534,262,266,129,4,255);
  $display("BLITTER_SIM_PASS tests=%0d random_backpressure=1 pixel_and_guard_compare=1 cache_hits=%0d saved=%0d",tests,dut.cache_hits,dut.cache_saved);$finish;
 end
 initial begin #20000000;$fatal(1,"Simulation global timeout");end
endmodule
