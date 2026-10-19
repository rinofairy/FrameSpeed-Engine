`timescale 1ns/1ps
module tb_scanline;
 reg clk=0;always #5 clk=~clk;
 reg reset=1;
 reg [15:0] paddr=0;reg psel=0,penable=0,pwrite=0;reg [31:0] pwdata=0;
 wire [31:0] prdata;wire pready,pslverror;
 wire [31:0] awaddr,araddr,wdata;wire [7:0] awlen,arlen;
 wire [2:0] awsize,arsize;wire [1:0] awburst,arburst;wire [3:0] wstrb;
 wire awvalid,arvalid,wvalid,wlast,rready,bready;
 wire awready,arready,wready;
 reg [31:0] rdata=0;reg [1:0] rresp=0,bresp=0;reg rlast=0,rvalid=0,bvalid=0;
 fs_blitter dut(.*);
 localparam BASE=32'h00400000,SIZE=1048576;
 reg [7:0] mem[0:SIZE-1],expected[0:SIZE-1];
 reg reading=0,writing=0,inject_read_error=0,inject_background_error=0;
 reg inject_prefetch_error=0,inject_write_error=0,prefetch_read=0,no_stalls=0;
 reg command_dma_on=0,command_read=0,inject_command_error=0,inject_command_early_last=0;
 integer cmd_store=524284,cmd_address=BASE+524284,command_ar_seen=0;
 integer extra_pf_delay=0,pf_delay=0,extra_b_delay=0,force_w_stall=0;
 integer parallel_draw_cycles=0,parallel_bg_cycles=0,single_cycles,dual_cycles;
 integer ra,rl,ri,wa,wl,wi,cycle=0,bdelay=0,seed=32'h54327,random_value;
 reg stall_r=0,stall_w=0,stall_a=0;
 assign arready=!reading && !rvalid && !stall_a;
 assign awready=!writing && !bvalid && bdelay==0 && !stall_a;
 assign wready=writing && !stall_w && force_w_stall==0;
 reg blocked_w=0;reg [36:0] held_w;
 reg blocked_ar=0,blocked_aw=0;reg [39:0] held_ar,held_aw;
 integer b;
 always @(posedge clk)begin
  cycle<=cycle+1;
  random_value=$random(seed);stall_a<=!no_stalls && random_value[0];stall_w<=!no_stalls && random_value[2];stall_r<=!no_stalls && random_value[4];
  if(!reset)begin
   if(force_w_stall>0)force_w_stall<=force_w_stall-1;
   if(dut.u_scan.drawing_read && dut.u_scan.flushing_read && dut.u_scan.compose_bank==dut.u_scan.writer_bank)$fatal(1,"row read ownership conflict");
   if((dut.u_scan.line_even_write || dut.u_scan.line_odd_write) && dut.u_scan.row_ready[dut.u_scan.compose_bank])$fatal(1,"overwrote pending row");
   if(dut.u_scan.state==9 && dut.u_scan.wx_state==4)parallel_draw_cycles<=parallel_draw_cycles+1;
   if((dut.u_scan.state==5 || dut.u_scan.state==23 || dut.u_scan.state==26) && dut.u_scan.wx_state==4)parallel_bg_cycles<=parallel_bg_cycles+1;
   if(blocked_w && (!wvalid || {wdata,wstrb,wlast}!==held_w))$fatal(1,"W changed under backpressure");
   blocked_w<=wvalid && !wready;held_w<={wdata,wstrb,wlast};
   if(blocked_ar && (!arvalid || {araddr,arlen}!==held_ar))$fatal(1,"AR changed under backpressure");
   if(blocked_aw && (!awvalid || {awaddr,awlen}!==held_aw))$fatal(1,"AW changed under backpressure");
   blocked_ar<=arvalid&&!arready;held_ar<={araddr,arlen};
   blocked_aw<=awvalid&&!awready;held_aw<={awaddr,awlen};
   if(arvalid && arready)begin
    if(arsize!=2 || arburst!=1 || araddr[1:0]!=0 || araddr[11:0]+(arlen+1)*4>4096)$fatal(1,"Bad AR burst");
    ra<=araddr-BASE;rl<=arlen+1;ri<=0;reading<=1;
    command_read<=dut.u_scan.state==28;
    if(dut.u_scan.state==28)command_ar_seen<=command_ar_seen+1;
    prefetch_read<=dut.u_scan.pf_state==2;
    pf_delay<=dut.u_scan.pf_state==2?extra_pf_delay:0;
   end
   if(pf_delay>0)pf_delay<=pf_delay-1;
   if(reading && !rvalid && !stall_r && pf_delay==0)begin
    if(ra+ri*4<0 || ra+ri*4+3>=SIZE)$fatal(1,"Read outside test RAM");
    rdata<={mem[ra+ri*4+3],mem[ra+ri*4+2],mem[ra+ri*4+1],mem[ra+ri*4]};
    rvalid<=1;rlast<=ri==rl-1 || (inject_command_early_last && command_read && ri==0);
    rresp<=(inject_read_error || (inject_background_error && ra>=32768) ||
      (inject_prefetch_error && prefetch_read) || (inject_command_error && command_read && ri==2)) ? 2 : 0;
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
    if(wlast)begin writing<=0;bdelay<=3+extra_b_delay;end
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
 reg span_on=0;integer span_rowsel[0:511];integer baseline_pixels,optimized_pixels,baseline_cycles,ry,rx,left_edge,right_edge,entry_start,entry_end;
 integer cw=64,ch=48,ds=132,base_dst=131068,background='h0843;
 integer nd=0,burst=16,texwords=4096,bg_source=65532;
 integer dx[0:511],dy[0:511],dw[0:511],dh[0:511],dsrc[0:511],dstride[0:511],deffect[0:511],da[0:511];
 integer i,j,k,x,y,sp,dp,actual,expected_error,waits,frame_cycles,legacy_cycles;
 integer seed_cases=123456;
 reg [15:0] fg,bg,px;
 function [15:0] blend(input [15:0] f,b,input integer a);
  integer rr,gg,bb;
  begin
   rr=(((f>>11)&31)*a+((b>>11)&31)*(255-a)+127)/255;
   gg=(((f>>5)&63)*a+((b>>5)&63)*(255-a)+127)/255;
   bb=((f&31)*a+(b&31)*(255-a)+127)/255;
   blend=(rr<<11)|(gg<<5)|bb;
  end
 endfunction
 task wait_scan(input integer err);
  integer timeout;begin
   timeout=0;paddr='h84;
   while((prdata&3)!=2 && timeout<4000000)begin @(negedge clk);timeout=timeout+1;end
   paddr='hbc;#1;
   if(timeout>=4000000 || prdata!=err)$fatal(1,"scan error got=%0d expected=%0d timeout=%0d state=%0d",prdata,err,timeout,dut.u_scan.state);
   paddr='hcc;#1;if(prdata!=sequence_number)$fatal(1,"scan completion sequence");
   if(reading || rvalid || writing || bvalid)$fatal(1,"scan finished with outstanding AXI");
   tests=tests+1;$display("SCAN_CASE_PASS n=%0d error=%0d",tests,err);$fflush();
  end
 endtask
 task load_texture(input integer words,err);
  begin sequence_number=sequence_number+1;
   apb_write('ha0,BASE);apb_write('ha4,words);apb_write('hb8,burst);
   apb_write('hac,sequence_number);apb_write('h88,2);wait_scan(err);
  end
 endtask
 task descriptor(input integer n,s,stride,w,h,xx,yy,op,alpha);
  begin dsrc[n]=s;dstride[n]=stride;dw[n]=w;dh[n]=h;dx[n]=xx;dy[n]=yy;deffect[n]=op;da[n]=alpha;end
 endtask
 task command_word(input integer index,input [31:0] value);
  integer bb;begin
   for(bb=0;bb<4;bb=bb+1)begin
    mem[cmd_store+index*4+bb]=value[bb*8+:8];
    expected[cmd_store+index*4+bb]=value[bb*8+:8];
   end
  end
 endtask
 task send_frame(input integer err);
  integer n,eff,source_word;begin
   sequence_number=sequence_number+1;apb_write('hb0,0);
   for(n=0;n<nd;n=n+1)begin
    eff='hf81f|(da[n]<<16)|(((deffect[n]==2 || deffect[n]==4)?1:0)<<24)|((deffect[n]>=3?1:0)<<25);
    source_word=(dstride[n]<<16)|dsrc[n];
    if(span_on)begin eff=eff|((span_rowsel[n]&63)<<26);source_word=source_word|32'h8000|((span_rowsel[n]>>6)<<30);end
    if(command_dma_on)begin
     command_word(n*4,source_word);command_word(n*4+1,(dy[n]<<16)|dx[n]);
     command_word(n*4+2,(dh[n]<<16)|dw[n]);command_word(n*4+3,eff);
    end else begin
     apb_write('hb4,source_word);apb_write('hb4,(dy[n]<<16)|dx[n]);
     apb_write('hb4,(dh[n]<<16)|dw[n]);apb_write('hb4,eff);
    end
   end
   apb_write('h8c,BASE+base_dst);apb_write('h90,ds);apb_write('h94,(ch<<16)|cw);
   apb_write('h98,nd);apb_write('h9c,background);apb_write('hdc,BASE+bg_source);apb_write('hb8,burst);
   apb_write('ha8,cmd_address);apb_write('hac,sequence_number);apb_write('h88,command_dma_on?3:1);wait_scan(err);
   paddr='hc0;#1;frame_cycles=prdata;
  end
 endtask
 task reference;
  integer a,n,xx,yy,srcp,dstp;reg [15:0] f,b,p;
  begin
   for(a=0;a<SIZE;a=a+1)expected[a]=mem[a];
   for(yy=0;yy<ch;yy=yy+1)for(xx=0;xx<cw;xx=xx+1)begin
    dstp=base_dst+yy*ds+xx*2;if(background[31])begin
     srcp=((background&65535)+((((background>>16)&511)+yy)/8)*64+xx/10)*2;
     expected[dstp]=mem[srcp];expected[dstp+1]=mem[srcp+1];
    end else if(background[30])begin srcp=bg_source+yy*ds+xx*2;expected[dstp]=mem[srcp];expected[dstp+1]=mem[srcp+1];
    end else begin expected[dstp]=background;expected[dstp+1]=background>>8;end
   end
   for(n=0;n<nd;n=n+1)for(yy=0;yy<dh[n];yy=yy+1)for(xx=0;xx<dw[n];xx=xx+1)begin
    srcp=(dsrc[n]+yy*dstride[n]+xx)*2;dstp=base_dst+(dy[n]+yy)*ds+(dx[n]+xx)*2;
    f={mem[srcp+1],mem[srcp]};b={expected[dstp+1],expected[dstp]};
    p=deffect[n]>=3?blend(f,b,da[n]):f;
    if(!((deffect[n]==2 || deffect[n]==4) && f==16'hf81f))begin expected[dstp]=p;expected[dstp+1]=p>>8;end
   end
  end
 endtask
 task compare;
  integer a;begin
   for(a=0;a<SIZE;a=a+1)if(mem[a]!==expected[a])$fatal(1,"frame pixel/guard mismatch case=%0d address=%h got=%h expected=%h",tests,a,mem[a],expected[a]);
   paddr='hc8;#1;if(prdata!=cw*ch*2)$fatal(1,"frame write traffic wrong");
   paddr='hc4;#1;if(prdata!=(background[30]?cw*ch*2:0)+(command_dma_on?nd*16:0))$fatal(1,"DDR read traffic wrong");
   paddr='hf8;#1;if(prdata!=(command_dma_on?nd*16:0))$fatal(1,"command DMA traffic wrong");
  end
 endtask
 task legacy_cmd(input integer op,s,d,ss,st,w,h,a);
  integer timeout;begin
   sequence_number=sequence_number+1;
   apb_write(4,6);apb_write('hc,op);apb_write('h10,BASE+s);apb_write('h14,BASE+d);
   apb_write('h18,ss);apb_write('h1c,st);apb_write('h20,w);apb_write('h24,h);
   apb_write('h28,background);apb_write('h2c,'hf81f);apb_write('h30,a);apb_write('h34,sequence_number);
   apb_write(8,1);timeout=0;paddr=4;
   while((prdata&3)!=2 && timeout<4000000)begin @(negedge clk);timeout=timeout+1;end
   if(timeout>=4000000 || (prdata&4))$fatal(1,"legacy command failure");
  end
 endtask
 task perf_compare;
  integer n,start_cycle,legacy_bytes;begin
   // Same resident 32x32 texture in both paths. Legacy foreground cache preload.
   apb_write('h54,3);apb_write('hc,5);apb_write('h10,BASE);
   apb_write('h20,512);apb_write('h24,1);apb_write('h28,0);
   apb_write('h34,99999);apb_write(8,1);paddr=4;
   waits=0;while((prdata&3)!=2 && waits<100000)begin @(negedge clk);waits=waits+1;end
   if(waits>=100000 || (prdata&4))$fatal(1,"legacy preload failure");
   start_cycle=cycle;legacy_bytes=0;
   legacy_cmd(0,0,base_dst,0,ds,cw,ch,0);
   for(n=0;n<nd;n=n+1)begin
    legacy_cmd(deffect[n],dsrc[n]*2,base_dst+dy[n]*ds+dx[n]*2,dstride[n]*2,ds,dw[n],dh[n],da[n]);
    paddr='h40;#1;legacy_bytes=legacy_bytes+prdata;
   end
   legacy_cycles=cycle-start_cycle;
   for(i=0;i<SIZE;i=i+1)if(mem[i]!==expected[i])$fatal(1,"legacy reference mismatch");
   start_cycle=cycle;send_frame(0);compare;
   $display("SCAN_PERF objects=%0d width=%0d height=%0d burst=%0d legacy_cycles=%0d batch_total_cycles=%0d batch_core_cycles=%0d",nd,dw[0],dh[0],burst,legacy_cycles,cycle-start_cycle,frame_cycles);
  end
 endtask
 initial begin
  for(i=0;i<SIZE;i=i+1)mem[i]=(i*13)^'ha5;
  for(i=0;i<8192;i=i+1)begin px=(i%5==0)?16'hf81f:((i*31)^16'h5555);mem[i*2]=px;mem[i*2+1]=px>>8;end
  repeat(5)@(negedge clk);reset=0;
  load_texture(4096,0);
  // Empty frame; overlap, order, odd coordinates/widths, padded source/destination.
  reference;send_frame(0);compare;
  nd=4;
  descriptor(0,1,37,31,17,1,2,4,128);descriptor(1,99,41,33,19,7,5,4,127);
  descriptor(2,4096,7,1,7,63,0,3,254);descriptor(3,1,37,31,17,1,2,2,0);
  reference;send_frame(0);compare;
  for(k=0;k<6;k=k+1)begin
   case(k)0:da[0]=0;1:da[0]=1;2:da[0]=127;3:da[0]=128;4:da[0]=254;5:da[0]=255;endcase
   deffect[0]=3;reference;send_frame(0);compare;
  end
  // Randomized descriptors and mixtures; deliberately many overlaps.
  for(k=0;k<12;k=k+1)begin
   nd=1+(k*17)%90;
   for(j=0;j<nd;j=j+1)begin
    actual=$random(seed_cases)&32'h7fffffff;x=1+actual%41;y=1+(actual>>8)%29;
    descriptor(j,(actual>>4)%100,43,x,y,(actual>>2)%(cw-x+1),(actual>>6)%(ch-y+1),1+j%4,(actual>>9)%256);
   end
   burst=1<<(k%6);reference;send_frame(0);compare;
  end
  // DMA error drains, invalid descriptors rejected before any destination writes.
  nd=1;descriptor(0,8191,32,32,1,0,0,4,128);
  for(i=0;i<SIZE;i=i+1)expected[i]=mem[i];send_frame(4);
  for(i=0;i<SIZE;i=i+1)if(mem[i]!==expected[i])$fatal(1,"invalid frame wrote memory");
  descriptor(0,0,32,32,32,0,0,4,128);
  inject_write_error=1;send_frame(6);inject_write_error=0;
  inject_read_error=1;load_texture(4096,5);inject_read_error=0;
  paddr='hd0;#1;if(prdata!=0)$fatal(1,"failed texture load marked valid");
  load_texture(4096,0);
  // Busy reconfiguration must signal APB error and preserve running state.
  nd=0;apb_write('h98,0);sequence_number=sequence_number+1;apb_write('hac,sequence_number);
  apb_write('h88,1);
  @(negedge clk);paddr='h8c;psel=1;penable=1;pwrite=1;pwdata=0;#1;
  if(!pslverror)$fatal(1,"busy scan write accepted");
  @(negedge clk);paddr=8;pwdata=1;#1;if(!pslverror)$fatal(1,"cross-core start accepted");
  @(negedge clk);psel=0;penable=0;pwrite=0;wait_scan(0);
  // Cached static background: odd band origins, sprite overlap, AXI stalls.
  load_texture(4096,0);nd=2;cw=640;ch=54;ds=1280;base_dst=131072;
  descriptor(0,0,32,32,32,1,3,4,128);descriptor(1,0,32,32,32,19,11,4,192);
  for(k=0;k<8;k=k+1)begin
   background=32'h80000000|(k*54<<16)|3904;reference;send_frame(0);compare;
  end
  nd=0;cw=2;ch=1;background=32'h80000000|3904;reference;send_frame(0);compare;
  background=32'h80000001;send_frame(7);
  background=32'h82000000;send_frame(7);
  background=32'h80000000|(479<<16)|8190;send_frame(7);
  background=32'h80000000|(480<<16);send_frame(7);
  // Native-resolution DDR background: no extra clear or texture readback.
  nd=2;cw=64;ch=48;ds=132;base_dst=262140;bg_source=65532;background=32'h40000000;no_stalls=0;
  descriptor(0,0,32,31,17,1,3,4,128);descriptor(1,99,41,33,19,7,5,4,192);
  for(k=0;k<6;k=k+1)begin burst=1<<k;reference;send_frame(0);compare;end
  // Write failure while next-row background read is outstanding must drain.
  inject_write_error=1;send_frame(6);inject_write_error=0;
  nd=512;for(j=0;j<nd;j=j+1)descriptor(j,1,1,1,1,j%64,(j/64)%48,4,128);
  reference;send_frame(0);compare;
  nd=0;cw=640;ch=54;ds=1280;burst=32;reference;send_frame(0);compare;
  nd=1;cw=2;ch=1;ds=4;descriptor(0,1,1,1,1,1,0,3,0);reference;send_frame(0);compare;
  background=32'h40000001;send_frame(7);background=32'h40000000;
  bg_source=base_dst;send_frame(7);bg_source=65533;send_frame(7);
  bg_source='h7ffffc;cw=64;ch=48;ds=132;send_frame(7);bg_source=65532;
  inject_read_error=1;send_frame(5);inject_read_error=0;reference;send_frame(0);compare;
  // Source-local row intervals are shared across arbitrary cropped descriptors.
  cw=64;ch=32;ds=132;base_dst=262140;background='h0843;burst=32;no_stalls=0;
  for(ry=0;ry<9;ry=ry+1)for(rx=0;rx<17;rx=rx+1)begin
   left_edge=1+ry%4;right_edge=13-left_edge;
   px=(ry==0 || ry==8 || rx<left_edge || rx>=right_edge || rx==6)?16'hf81f:(16'had50+ry*19+rx);
   mem[(501+ry*17+rx)*2]=px;mem[(501+ry*17+rx)*2+1]=px>>8;
  end
  load_texture(4096,0);apb_write('he0,0);apb_write('hec,'hf81f);
  for(ry=0;ry<200;ry=ry+1)apb_write('he4,0);
  for(ry=0;ry<9;ry=ry+1)begin
   left_edge=1+ry%4;right_edge=13-left_edge;
   if(ry==0 || ry==8)begin left_edge=0;right_edge=0;end
   entry_start=501+ry*17+left_edge;entry_end=501+ry*17+right_edge;
   apb_write('he4,entry_start|(entry_end<<13));
  end
  paddr='he8;#1;if(prdata!=209)$fatal(1,"span table size");
  nd=3;descriptor(0,501,17,13,9,1,1,4,128);span_rowsel[0]=200;
  descriptor(1,520,17,11,7,11,9,4,192);span_rowsel[1]=201;
  descriptor(2,556,17,7,4,7,2,4,1);span_rowsel[2]=203;
  span_on=0;reference;send_frame(0);compare;baseline_cycles=frame_cycles;
  paddr='hd4;#1;baseline_pixels=prdata;
  span_on=1;reference;send_frame(0);compare;paddr='hd4;#1;optimized_pixels=prdata;
  if(optimized_pixels>=baseline_pixels)$fatal(1,"spans did not skip pixels");
  paddr='hf4;#1;if(prdata!=baseline_pixels-optimized_pixels)$fatal(1,"skip counter mismatch");
  $display("SPAN_PERF baseline_pixels=%0d span_pixels=%0d baseline_cycles=%0d span_cycles=%0d",baseline_pixels,optimized_pixels,baseline_cycles,frame_cycles);
  for(k=0;k<4;k=k+1)begin
   for(j=0;j<nd;j=j+1)begin deffect[j]=k+1;da[j]=k==0?0:k==1?1:k==2?128:255;end
   reference;send_frame(0);compare;
   if(k==0 || k==2)begin paddr='hd4;#1;if(prdata!=baseline_pixels)$fatal(1,"key-disabled span bypass failed");end
  end
  // A different runtime key falls back to the full rectangle, never stale skips.
  apb_write('hec,'h1234);reference;send_frame(0);compare;paddr='hd4;#1;
  if(prdata!=baseline_pixels)$fatal(1,"different key did not bypass metadata");
  apb_write('hec,'hf81f);
  // Missing row records are rejected before DDR destination writes.
  apb_write('he0,0);for(i=0;i<SIZE;i=i+1)expected[i]=mem[i];send_frame(8);
  for(i=0;i<SIZE;i=i+1)if(mem[i]!==expected[i])$fatal(1,"invalid metadata wrote memory");
  load_texture(4096,0);paddr='he8;#1;if(prdata!=0)$fatal(1,"atlas reload retained stale rows");
  // New command-list DMA: same composition, no CPU pixel writes, both
  // background modes, all allowed burst lengths, 4KiB-split descriptor lists.
  span_on=0;command_dma_on=1;nd=4;cw=64;ch=48;ds=132;burst=32;
  descriptor(0,1,37,31,17,1,2,4,128);descriptor(1,99,41,33,19,7,5,4,127);
  descriptor(2,4096,7,1,7,63,0,3,254);descriptor(3,1,37,31,17,1,2,2,0);
  for(k=0;k<12;k=k+1)begin
   burst=1<<(k%6);background=k<6?'h0843:32'h40000000;
   reference;send_frame(0);compare;
  end
  paddr='hfc;#1;if(prdata<2 || command_ar_seen<2)$fatal(1,"command list did not split at 4KiB");
  nd=512;burst=32;
  for(j=0;j<nd;j=j+1)descriptor(j,j%100,3,1,1,j%64,(j/64)%48,4,j%256);
  reference;send_frame(0);compare;
  nd=0;cmd_address=1;reference;send_frame(0);compare;cmd_address=BASE+cmd_store;
  nd=1;descriptor(0,1,37,31,17,1,2,4,128);
  // Invalid DMA ranges rejected without issuing any AXI read or DDR write.
  for(k=0;k<4;k=k+1)begin
   case(k)0:cmd_address=BASE-4;1:cmd_address=BASE+cmd_store+1;2:cmd_address=32'h00bffffc;3:cmd_address=32'hfffffffc;endcase
   for(i=0;i<SIZE;i=i+1)expected[i]=mem[i];j=command_ar_seen;send_frame(9);
   if(command_ar_seen!=j)$fatal(1,"invalid command source read memory");
   for(i=0;i<SIZE;i=i+1)if(mem[i]!==expected[i])$fatal(1,"invalid DMA source wrote pixels");
  end
  cmd_address=BASE+cmd_store;
  inject_command_early_last=1;for(i=0;i<SIZE;i=i+1)expected[i]=mem[i];send_frame(5);inject_command_early_last=0;
  for(i=0;i<SIZE;i=i+1)if(mem[i]!==expected[i])$fatal(1,"early RLAST command wrote pixels");
  // Move the source off the one-beat page tail to inject a middle-beat error.
  cmd_address=BASE+524288;cmd_store=524288;nd=4;
  for(j=0;j<nd;j=j+1)descriptor(j,1,37,31,17,1,2,4,128);
  inject_command_error=1;for(i=0;i<SIZE;i=i+1)expected[i]=mem[i];send_frame(5);inject_command_error=0;
  for(i=0;i<SIZE;i=i+1)if(mem[i]!==expected[i])$fatal(1,"RRESP command error wrote pixels");
  reference;send_frame(0);compare;
  // Hardware still validates fetched descriptors before any destination write.
  nd=1;descriptor(0,8191,32,32,1,0,0,4,128);
  for(i=0;i<SIZE;i=i+1)expected[i]=mem[i];send_frame(4);
  for(i=0;i<SIZE;i=i+1)if(mem[i]!==expected[i])$fatal(1,"invalid fetched descriptor wrote pixels");
  command_dma_on=0;nd=1;descriptor(0,1,37,31,17,1,2,4,128);reference;send_frame(0);compare;
  // Same hardware/descriptor/pixels, runtime serial vs ping-pong writer.
  no_stalls=1;cw=640;ch=54;ds=1280;base_dst=262140;burst=32;
  background=32'h40000000;nd=16;
  for(j=0;j<nd;j=j+1)descriptor(j,1,37,24,24,(j*37)%617,(j*7)%31,4,192);
  apb_write('hf0,0);paddr='hf0;#1;if(prdata!=32'h00040100)$fatal(1,"serial mode control");
  reference;send_frame(0);compare;single_cycles=frame_cycles;
  parallel_draw_cycles=0;parallel_bg_cycles=0;
  apb_write('hf0,1);reference;send_frame(0);compare;dual_cycles=frame_cycles;
  if(dual_cycles>=single_cycles || parallel_draw_cycles==0)$fatal(1,"no parallel composition/writeback benefit");
  $display("DUAL_LINE_PERF serial_cycles=%0d dual_cycles=%0d overlap_draw=%0d overlap_bg=%0d",single_cycles,dual_cycles,parallel_draw_cycles,parallel_bg_cycles);
  // Full and partial rows, delayed read/write responses and ownership stalls.
  no_stalls=0;extra_b_delay=37;extra_pf_delay=23;force_w_stall=1000;
  reference;send_frame(0);compare;
  cw=2;ch=5;ds=12;nd=0;reference;send_frame(0);compare;
  cw=640;ch=1;ds=1280;reference;send_frame(0);compare;
  cw=64;ch=48;ds=132;nd=1;descriptor(0,1,37,31,17,1,2,4,128);
  inject_write_error=1;send_frame(6);inject_write_error=0;
  inject_prefetch_error=1;send_frame(5);inject_prefetch_error=0;
  // A following job must start without stale queued rows or bus ownership.
  extra_b_delay=0;extra_pf_delay=0;reference;send_frame(0);compare;
  apb_write('hf0,0);reference;send_frame(0);compare;
  apb_write('hf0,1);reference;send_frame(0);compare;
  $display("SCANLINE_SIM_PASS tests=%0d",tests);$finish;
 end
 initial begin #250000000;$fatal(1,"global timeout");end
endmodule
