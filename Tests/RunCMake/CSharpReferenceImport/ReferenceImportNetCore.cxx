// clang-format off

using namespace System;

#using <ImportLibMixed.dll>
#using <ImportLibNetCore.dll>
#using <ImportLibCSharp.dll>

int main()
{
  Console::WriteLine("ReferenceImportNetCore");
  ImportLibMixed::Message();
  ImportLibNetCore::Message();
  ImportLibCSharp::Message();
};
