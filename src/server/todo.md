* rename field/new to session/new 
* add session/stop POST handler to quit midgame or on win/lose -- remove session and allocated field
* add session/restart POST handler to finish the current game and start a new one with the same field id
* when session created – save time as start time in session
* add session/time GET handler to return elapsed time since session start
* add fully_revealed field to action/flag and action/reveal replies – so client doesn't have to call field/fully_revealed to know if game is over
* add action/confirm POST handler (name subject to change) to check non-zero revealed cell's neighbors (middle-click functionality)