import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
public class Focus extends GhidraScript {
 public void run() throws Exception {
  DecompInterface d=new DecompInterface(); d.openProgram(currentProgram);
  for(String a:getScriptArgs()) { Function f=getFunctionContaining(toAddr(a)); if(f==null) f=getFunctionAt(toAddr(a)); println("FOCUS " + a + " " + f); if(f!=null) { DecompileResults r=d.decompileFunction(f,60,monitor); if(r.decompileCompleted()) println(r.getDecompiledFunction().getC()); else println("FAILED " + r.getErrorMessage()); }} d.dispose();
 }
}
