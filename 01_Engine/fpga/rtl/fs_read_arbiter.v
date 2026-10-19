// FrameSpeed: two 32-bit AXI read clients, one burst outstanding.
// Deadline-driven scanout (b) has priority at burst boundaries.
// Selection remains held once offered: never preempt an AXI transaction.
// Scanout requests only 320 words per line, leaving idle time for blitter (a).
`default_nettype none
module fs_read_arbiter(
 input wire clk,reset,
 input wire [31:0] a_addr,b_addr,input wire [7:0] a_len,b_len,
 input wire a_valid,b_valid,output wire a_ready,b_ready,
 output wire [31:0] a_data,b_data,output wire [1:0] a_resp,b_resp,
 output wire a_last,b_last,a_rvalid,b_rvalid,input wire a_rready,b_rready,
 output wire [31:0] araddr,output wire [7:0] arlen,output wire arvalid,input wire arready,
 input wire [31:0] rdata,input wire [1:0] rresp,input wire rlast,rvalid,output wire rready
);
 reg [1:0] state;reg owner;
 assign araddr=owner?b_addr:a_addr;assign arlen=owner?b_len:a_len;
 assign arvalid=state==1 && (owner?b_valid:a_valid);
 assign a_ready=state==1 && !owner && arready;
 assign b_ready=state==1 && owner && arready;
 assign a_data=rdata;assign b_data=rdata;assign a_resp=rresp;assign b_resp=rresp;
 assign a_last=rlast;assign b_last=rlast;
 assign a_rvalid=state==2 && !owner && rvalid;
 assign b_rvalid=state==2 && owner && rvalid;
 assign rready=state==2 && (owner?b_rready:a_rready);
 always @(posedge clk) if(reset) begin state<=0;owner<=0;end else case(state)
  0: if(a_valid || b_valid) begin owner<=b_valid;state<=1;end
  1: if(arvalid && arready)state<=2;
  2: if(rvalid && rready && rlast)begin state<=0;end
  default:state<=0;
 endcase
endmodule
`default_nettype wire
