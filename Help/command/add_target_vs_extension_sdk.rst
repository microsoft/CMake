add_target_vs_extension_sdk
--------------------------

Add a reference to Extension SDK in the target Visual Studio project.

::

  add_target_vs_extension_sdk(target
                              extensionName
                              extensionVersion)

Adds a reference to an Extension SDK in the target Visual Studio project.
``extensionName`` is the name of extension (for example ``Bing.Maps.Xaml``).
``extensionVersion`` is the version of Extension SDK that will be referenced. 
(for example ``1.313.0825.0``).