
v1.0.1
- Add screen backlight settings
- Add font download management function
- Resolve the issue of file transmission errors via WIFI

v1.0.2

- Added support for Chinese books. #5
- Optimize the screen refresh method to reduce screen flickering
- Solve the problem of slow page loading when reading Chinese books
- Correct selecting tabs on Settings. #7
- Correct overriding already rendered images when Text Anti-Aliasing is enabled. #8

v1.0.3

- Add the bookmark function to the EPUB file. You can access the bookmarks by using the `Toggle Bookmark` option from the reader's menu. 

- Add the "whether the current page has been bookmarked" status to the status bar of the reading interface.

- Add the `Recent Book` option. Long press to remove a single book. You can also choose to automatically remove it after finishing reading. 

- Add the main page clock, read the page status bar clock, and set it through `Settings -> Reader -> Customise Status Bar`
    - Go to `Settings > System > Time Zone` and select the current region.
    - Go to `WiFi Networks` and connect to the network once. Let it automatically perform NTP time synchronization and write the RTC data.

If the time is reset after being reinitialized, an operation that does not 
rely on the public network NTP can be performed:
- Connect the device to WiFi, and open the device's web page once using a mobile phone or computer. `http://deviceIP/`, `/files/`, `/settings/`, and `/fonts` are all available.

- Quick Menu Feature #15
- System Chapter Font Footer Fix #16
- System-wide Hide Clock Option #17
- Flip up/down buttons and screen option #18
- Resume Book on Wake Fix + Making Optional #19






