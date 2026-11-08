import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.listing.*;

public class Report extends GhidraScript {
 public void run() throws Exception {
  FunctionManager fm = currentProgram.getFunctionManager();
  ReferenceManager rm = currentProgram.getReferenceManager();
  DecompInterface dec = new DecompInterface(); dec.openProgram(currentProgram);
  println("REPORT PROGRAM " + currentProgram.getName() + " FUNCTIONS " + fm.getFunctionCount());
  SymbolIterator si = currentProgram.getSymbolTable().getAllSymbols(true);
  while(si.hasNext()) {
   Symbol s = si.next(); String n=s.getName();
   if(n.startsWith("_SIM_") || n.equals("DirectDrawCreate") || n.equals("DirectSoundCreate") || n.equals("DirectPlayCreate") || n.equals("LoadLibraryA") || n.equals("GetProcAddress")) {
    println("REPORT SYMBOL " + n + " " + s.getAddress() + " " + s.getSymbolType());
    for(Reference ref: rm.getReferencesTo(s.getAddress())) println("REPORT REF " + n + " " + ref.getFromAddress() + " " + fm.getFunctionContaining(ref.getFromAddress()));
    Function f=fm.getFunctionAt(s.getAddress());
    if(f!=null && n.startsWith("_SIM_")) { DecompileResults d=dec.decompileFunction(f,30,monitor); if(d.decompileCompleted()) println("REPORT DECOMPILE " + n + "\n" + d.getDecompiledFunction().getC()); }
   }
  }
  DataIterator it=currentProgram.getListing().getDefinedData(true);
  while(it.hasNext()) {
   Data d=it.next(); Object v=d.getValue(); if(!(v instanceof String)) continue;
   String s=((String)v).toLowerCase();
   if(s.contains("a10sim.dll") || s.contains("_sim_init@4") || s.contains("_sim_processevent@16") || s.contains("cdirectdrawscreen") || s.contains("cdirectsound")) {
    println("REPORT STRING " + d.getAddress() + " " + v);
    for(Reference ref: rm.getReferencesTo(d.getAddress())) println("REPORT STRINGREF " + d.getAddress() + " " + ref.getFromAddress() + " " + fm.getFunctionContaining(ref.getFromAddress()));
   }
  }
  dec.dispose();
 }
}
