// Two RGB565 pixels per clock, uniform 8-bit alpha. AI-assisted project RTL.
// Three register stages: products -> sum+bias -> exact rounded /255.
// Operates on native 5/6/5 components (not gamma-correct blending).
`timescale 1ns/1ps
`default_nettype none
module fs_alpha565(input wire clk,reset,in_valid,
 input wire [31:0] fg,bg,input wire [7:0] alpha,
 output reg out_valid,output reg [31:0] pixel);
 reg v0,v1;
 reg [15:0] front_product[0:5],back_product[0:5],biased_sum[0:5];
 wire [7:0] inverse_alpha=8'd255-alpha;
 wire [7:0] front[0:5],back[0:5];
 genvar p;
 generate for(p=0;p<2;p=p+1)begin:g_pixel
  assign front[p*3]={3'd0,fg[p*16+11+:5]};
  assign front[p*3+1]={2'd0,fg[p*16+5+:6]};
  assign front[p*3+2]={3'd0,fg[p*16+:5]};
  assign back[p*3]={3'd0,bg[p*16+11+:5]};
  assign back[p*3+1]={2'd0,bg[p*16+5+:6]};
  assign back[p*3+2]={3'd0,bg[p*16+:5]};
 end endgenerate
 function [7:0] rounded_component(input [15:0] biased);
  reg [16:0] corrected;
  begin corrected={1'b0,biased}+{9'd0,biased[15:8]};rounded_component=corrected[15:8];end
 endfunction
 wire [7:0] c[0:5];
 generate for(p=0;p<6;p=p+1)begin:g_round
  assign c[p]=rounded_component(biased_sum[p]);
 end endgenerate
 integer i;
 always @(posedge clk)begin
  if(reset)begin
   v0<=0;v1<=0;out_valid<=0;pixel<=0;
   for(i=0;i<6;i=i+1)begin front_product[i]<=0;back_product[i]<=0;biased_sum[i]<=0;end
  end else begin
   v0<=in_valid;v1<=v0;out_valid<=v1;
   if(in_valid)for(i=0;i<6;i=i+1)begin
    front_product[i]<=front[i]*alpha;back_product[i]<=back[i]*inverse_alpha;
   end
   if(v0)for(i=0;i<6;i=i+1)biased_sum[i]<=front_product[i]+back_product[i]+16'd128;
   if(v1)pixel<={c[3][4:0],c[4][5:0],c[5][4:0],c[0][4:0],c[1][5:0],c[2][4:0]};
  end
 end
endmodule
`default_nettype wire
