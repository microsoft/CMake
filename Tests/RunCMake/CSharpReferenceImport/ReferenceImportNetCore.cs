using System;

public class ReferenceImport
{
  public static void Main()
  {
    Console.WriteLine("ReferenceImportCSharpNetCore");
    ImportLibMixed.Message();
    ImportLibNetCore.Message();
    ImportLibCSharp.Message();
  }
}
