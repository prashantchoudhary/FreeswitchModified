'use strict';

/* Controllers */

var videoQuality = [{
  id: 'qvga',
  label: 'QVGA 320x240',
  width: 320,
  height: 240
}, {
  id: 'vga',
  label: 'VGA 640x480',
  width: 640,
  height: 480
}, {
  id: 'qvga_wide',
  label: 'QVGA WIDE 320x180',
  width: 320,
  height: 180
}, {
  id: 'vga_wide',
  label: 'VGA WIDE 640x360',
  width: 640,
  height: 360
}, {
  id: 'hd',
  label: 'HD 1280x720',
  width: 1280,
  height: 720
}, {
  id: 'hhd',
  label: 'HHD 1920x1080',
  width: 1920,
  height: 1080
}, ];

var videoResolution = {
  qvga: {
    width: 320,
    height: 240
  },
  vga: {
    width: 640,
    height: 480
  },
  qvga_wide: {
    width: 320,
    height: 180
  },
  vga_wide: {
    width: 640,
    height: 360
  },
  hd: {
    width: 1280,
    height: 720
  },
  hhd: {
    width: 1920,
    height: 1080
  },
};

var bandwidth = [{
  id: '250',
  label: '250kb'
}, {
  id: '500',
  label: '500kb'
}, {
  id: '1024',
  label: '1mb'
}, {
  id: '1536',
  label: '1.5mb'
}, {
  id: '2048',
  label: '2mb'
}, {
  id: '5120',
  label: '5mb'
}, {
  id: '0',
  label: 'No Limit'
}, {
  id: 'default',
  label: 'Server Default'
}, ];

var vertoService = angular.module('vertoService', ['ngCookies']);

vertoService.service('verto', ['$rootScope', '$cookieStore', '$location', 'storage',
  function($rootScope, $cookieStore, $location, storage) {
    var data = {
      // Connection data.
      instance: null,
      connected: false,

      // Call data.
      call: null,
      shareCall: null,
      callState: null,
      conf: null,
      confLayouts: [],
      confRole: null,
      chattingWith: null,
      liveArray: null,

      // Settings data.
      videoDevices: [],
      audioDevices: [],
      shareDevices: [],
      videoQuality: [],
      extension: $cookieStore.get('verto_demo_ext'),
      name: $cookieStore.get('verto_demo_name'),
      email: $cookieStore.get('verto_demo_email'),
      cid: $cookieStore.get('verto_demo_cid'),
      textTo: $cookieStore.get('verto_demo_textto') || "1000",
      login: $cookieStore.get('verto_demo_login') || "1008",
      password: $cookieStore.get('verto_demo_passwd') || "1234",
      hostname: window.location.hostname,
      wsURL: ("wss://" + window.location.hostname + ":8082")
    };

    function cleanShareCall(that) {
      data.shareCall = null;
      data.callState = 'active';
      that.refreshDevices();
    }

    function cleanCall() {
      data.call = null;
      data.callState = null;
      data.conf = null;
      data.confLayouts = [];
      data.confRole = null;
      data.chattingWith = null;

      $rootScope.$emit('call.hangup', 'hangup');
    }

    function inCall() {
      $rootScope.$emit('page.incall', 'call');
    }

    function callActive(last_state) {
      $rootScope.$emit('call.active', last_state);
    }

    function calling() {
      $rootScope.$emit('call.calling', 'calling');
    }

    function incomingCall(number) {
      $rootScope.$emit('call.incoming', number);
    }

    function updateResolutions(supportedResolutions) {
      console.debug('Attempting to sync supported and available resolutions');

      var removed = 0;

      angular.forEach(videoQuality, function(resolution, id) {
        var supported = false;
        angular.forEach(supportedResolutions, function(res) {
          var width = res[0];
          var height = res[1];

          if(resolution.width == width && resolution.height == height) {
            supported = true;
          }
        });

        if(!supported) {
          delete videoQuality[id];
          ++removed;
        }
      });

      videoQuality.length = videoQuality.length - removed;
      data.videoQuality = videoQuality;
      data.vidQual = (videoQuality.length > 0) ? videoQuality[videoQuality.length - 1].id : null;

      return videoQuality;
    };

    var callState = {
      muteMic: false,
      muteVideo: false
    };

    return {
      data: data,
      callState: callState,

      // Options to compose the interface.
      videoQuality: videoQuality,
      videoResolution: videoResolution,
      bandwidth: bandwidth,

      refreshDevicesCallback : function refreshDevicesCallback() {
        data.videoDevices = [];
        data.shareDevices = [{
          id: 'screen',
          label: 'Screen'
        }];
        data.audioDevices = [];

        for (var i in jQuery.verto.videoDevices) {
          var device = jQuery.verto.videoDevices[i];
          if (!device.label) {
            data.videoDevices.push({
              id: 'Camera ' + i,
              label: 'Camera ' + i
            });
          } else {
            data.videoDevices.push({
              id: device.id,
              label: device.label || device.id
            });
          }

          // Selecting the first source.
          if (i == 0 && !storage.data.selectedVideo) {
            storage.data.selectedVideo = device.id;
          }

          if (!device.label) {
            data.shareDevices.push({
              id: 'Share Device ' + i,
              label: 'Share Device ' + i
            });
            continue;
          }

          data.shareDevices.push({
            id: device.id,
            label: device.label || device.id
          });
        }

        for (var i in jQuery.verto.audioInDevices) {
          var device = jQuery.verto.audioInDevices[i];
          // Selecting the first source.
          if (i == 0 && !storage.data.selectedAudio) {
            storage.data.selectedAudio = device.id;
          }

          if (!device.label) {
            data.audioDevices.push({
              id: 'Microphone ' + i,
              label: 'Microphone ' + i
            });
            continue;
          }
          data.audioDevices.push({
            id: device.id,
            label: device.label || device.id
          });
        }
        console.debug('Devices were refreshed, checking that we have cameras.');

        // This means that we cannot use video!
        if (data.videoDevices.length === 0) {
          console.log('No camera, disabling video.');
          data.canVideo = false;
          data.videoDevices.push({
            id: 'none',
            label: 'No camera'
          });
        } else {
          data.canVideo = true;
        }
      },

      refreshDevices: function(callback) {
        console.debug('Attempting to refresh the devices.');
        jQuery.verto.refreshDevices(this.refreshDevicesCallback);
      },

      /**
       * Updates the video resolutions based on settings.
       */
      refreshVideoResolution: function(resolutions) {
        console.debug('Attempting to refresh video resolutions.');

        if (data.instance) {
          var w = resolutions['bestResSupported'][0];
          var h = resolutions['bestResSupported'][1];

          if (h === 1080) {
            w = 1280;
            h = 720;
          }

          updateResolutions(resolutions['validRes']);
          data.instance.videoParams({
            minWidth: w,
            minHeight: h,
            maxWidth: w,
            maxHeight: h,
            minFrameRate: 15,
            vertoBestFrameRate: 30
          });
          videoQuality.forEach(function(qual){
            if (w === qual.width && h === qual.height) {
              if (storage.data.vidQual !== qual.id || storage.data.vidQual === undefined) {
                storage.data.vidQual = qual.id;
              }
            }

          });

        } else {
          console.debug('There is no instance of verto.');
        }
      },

      /**
       * Connects to the verto server. Automatically calls `onWSLogin`
       * callback set in the verto object.
       *
       * @param callback
       */
      connect: function(callback) {
        console.debug('Attempting to connect to verto.');
        var that = this;

        function startConference(v, dialog, pvtData) {
          $rootScope.$emit('call.video', 'video');
          data.chattingWith = pvtData.chatID;
          data.confRole = pvtData.role;

          var conf = new $.verto.conf(v, {
            dialog: dialog,
            hasVid: storage.data.useVideo,
            laData: pvtData,
            chatCallback: function(v, e) {
              var from = e.data.fromDisplay || e.data.from || "Unknown";
              var message = e.data.message || "";
              $rootScope.$emit('chat.newMessage', {
                from: from,
                body: message
              });
            },
            onBroadcast: function(v, conf, message) {
              console.log('>>> conf.onBroadcast:', arguments);
              if (message.action == 'response') {
                // This is a response with the video layouts list.
                if (message['conf-command'] == 'list-videoLayouts') {
                  data.confLayouts = message.responseData.sort();
                } else {
                  $rootScope.$emit('conference.broadcast', message);
                }
              }
            }
          });

          console.log('>>> conf.listVideoLayouts();');
          conf.listVideoLayouts();
          data.conf = conf;

          data.liveArray = new $.verto.liveArray(
            data.instance, pvtData.laChannel,
            pvtData.laName, {
              subParams: {
                callID: dialog ? dialog.callID : null
              }
            });

          data.liveArray.onErr = function(obj, args) {
            console.log('liveArray.onErr', obj, args);
          };

          data.liveArray.onChange = function(obj, args) {
            // console.log('liveArray.onChange', obj, args);

            switch (args.action) {
              case 'bootObj':
                $rootScope.$emit('members.boot', args.data);
                args.data.forEach(function(member){
                  var callId = member[0];
                  var status = angular.fromJson(member[1][4]);
                  if (callId === data.call.callID) {
                    $rootScope.$apply(function(){
                      data.mutedMic = status.audio.muted;
                      data.mutedVideo = status.video.muted;
                    });
                  }
                });
                break;
              case 'add':
                var member = [args.key, args.data];
                $rootScope.$emit('members.add', member);
                break;
              case 'del':
                var uuid = args.key;
                $rootScope.$emit('members.del', uuid);
                break;
              case 'clear':
                $rootScope.$emit('members.clear');
                break;
              case 'modify':
                var member = [args.key, args.data];
                $rootScope.$emit('members.update', member);
                break;
              default:
                console.log('NotImplemented', args.action);
            }
          };
        }

        function stopConference() {
          console.log('stopConference()');
          if (data.liveArray) {
            data.liveArray.destroy();
            console.log('Has data.liveArray.');
            $rootScope.$emit('members.clear');
            data.liveArray = null;

          } else {
            console.log('Doesn\'t found data.liveArray.');
          }
        }

        var callbacks = {
          onWSLogin: function(v, success) {
            data.connected = success;
            console.debug('Connected to verto server:', success);

            if (angular.isFunction(callback)) {
              callback(v, success);
            }
          },

          onMessage: function(v, dialog, msg, params) {
            console.debug('onMessage:', v, dialog, msg, params);

            switch (msg) {
              case $.verto.enum.message.pvtEvent:
                if (params.pvtData) {
                  switch (params.pvtData.action) {
                    case "conference-liveArray-join":
                      console.log("conference-liveArray-join");
                      startConference(v, dialog, params.pvtData);
                      break;
                    case "conference-liveArray-part":
                      console.log("conference-liveArray-part");
                      stopConference();
                      break;
                  }
                }
                break;
              /**
                * This is not being used for conferencing chat
                * anymore (see conf.chatCallback for that).
                */
              case $.verto.enum.message.info:
                var body = params.body;
                var from = params.from_msg_name || params.from;
                $rootScope.$emit('chat.newMessage', {
                  from: from,
                  body: body
                });
                break;
              default:
                break;
            }
          },

          onDialogState: function(d) {
            if (!data.call) {
              data.call = d;
              if (d.state.name !== 'ringing') {
                inCall();
              }
            }

            console.debug('onDialogState:', d);
            switch (d.state.name) {
              case "ringing":
                incomingCall(d.params.caller_id_number);
                break;
              case "trying":
                console.debug('Calling:', d.cidString());
                data.callState = 'trying';
                break;
              case "early":
                console.debug('Talking to:', d.cidString());
                data.callState = 'active';
                calling();
                break;
              case "active":
                console.debug('Talking to:', d.cidString());
                data.callState = 'active';
                callActive(d.lastState.name);
                break;
              case "hangup":
                console.debug('Call ended with cause: ' + d.cause);
                data.callState = 'hangup';
                break;
              case "destroy":
                console.debug('Destroying: ' + d.cause);
                if (d.params.screenShare) {
                  cleanShareCall(that);
                } else {
                  if (data.conf) {
                    data.liveArray.destroy();  
                    data.conf.destroy();
                  }
                  cleanCall();
                }
                break;
            }
          },

          onWSClose: function(v, success) {
            console.debug('onWSClose:', success);
          },

          onEvent: function(v, e) {
            console.debug('onEvent:', e);
          }
        };

        var that = this;
        function ourBootstrap() {
          // Checking if we have a failed connection attempt before
          // connecting again.
          that.refreshDevicesCallback();
          if (data.instance && !data.instance.rpcClient.socketReady()) {
              clearTimeout(data.instance.rpcClient.to);
              data.instance.logout();
          };
          data.instance = new jQuery.verto({
            login: data.login + '@' + data.hostname,
            passwd: data.password,
            socketUrl: data.wsURL,
            tag: "webcam",
            ringFile: "sounds/bell_ring2.wav",
            // TODO: Add options for this.
            audioParams: {
                googEchoCancellation: storage.data.googEchoCancellation || false,
                googNoiseSuppression: storage.data.googNoiseSuppression || false,
                googHighpassFilter: storage.data.googHighpassFilter || false
            },
            iceServers: storage.data.useSTUN
          }, callbacks);

          data.instance.deviceParams({
            useCamera: storage.data.selectedVideo,
            useMic: storage.data.selectedAudio,
            onResCheck: that.refreshVideoResolution
          });

        }

        $.verto.init({}, ourBootstrap);
      },

      /**
       * Login the client.
       *
       * @param callback
       */
      login: function(callback) {
        data.instance.loginData({
          login: data.login + '@' + data.hostname,
          passwd: data.password
        });
        data.instance.login();

        if (angular.isFunction(callback)) {
          callback(data.instance, true);
        }
      },

      /**
       * Disconnects from the verto server. Automatically calls `onWSClose`
       * callback set in the verto object.
       *
       * @param callback
       */
      disconnect: function(callback) {
        console.debug('Attempting to disconnect to verto.');

        data.instance.logout();
        data.connected = false;

        console.debug('Disconnected from verto server.');

        if (angular.isFunction(callback)) {
          callback(data.instance, data.connected);
        }
      },

      /**
       * Make a call.
       *
       * @param callback
       */
      call: function(destination, callback) {
        console.debug('Attempting to call destination ' + destination + '.');

        var call = data.instance.newCall({
          destination_number: destination,
          caller_id_name: data.name,
          caller_id_number: data.login,
          outgoingBandwidth: storage.data.outgoingBandwidth,
          incomingBandwidth: storage.data.incomingBandwidth,
          useVideo: storage.data.useVideo,
          useStereo: storage.data.useStereo,
          useCamera: storage.data.selectedVideo,
          useMic: storage.data.selectedAudio,
          dedEnc: storage.data.useDedenc,
          mirrorInput: storage.data.mirrorInput,
          userVariables: {
            email : storage.data.email,
            avatar: "http://gravatar.com/avatar/" + md5(storage.data.email) + ".png?s=600"
          }
        });

        data.call = call;

        data.mutedMic = false;
        data.mutedVideo = false;

        this.refreshDevices();

        if (angular.isFunction(callback)) {
          callback(data.instance, call);
        }
      },

      screenshare: function(destination, callback) {
        console.log('share screen video');

        var that = this;

        getScreenId(function(error, sourceId, screen_constraints) {
          var call = data.instance.newCall({
            destination_number: destination + '-screen',
            caller_id_name: data.name + ' (Screen)',
            caller_id_number: data.login + ' (Screen)',
            outgoingBandwidth: storage.data.outgoingBandwidth,
            incomingBandwidth: storage.data.incomingBandwidth,
            videoParams: screen_constraints.video.mandatory,
            useVideo: storage.data.useVideo,
            screenShare: true,
            dedEnc: storage.data.useDedenc,
            mirrorInput: storage.data.mirrorInput,
            userVariables: {
              email : storage.data.email,
              avatar: "http://gravatar.com/avatar/" + md5(storage.data.email) + ".png?s=600"
            }
          });

          data.shareCall = call;

          console.log('shareCall', data);

          data.mutedMic = false;
          data.mutedVideo = false;

          that.refreshDevices();

        });

      },

      screenshareHangup: function() {
        if (!data.shareCall) {
          console.debug('There is no call to hangup.');
          return false;
        }

        console.log('shareCall End', data.shareCall);
        data.shareCall.hangup();

        console.debug('The screencall was hangup.');

      },

      /**
       * Hangup the current call.
       *
       * @param callback
       */
      hangup: function(callback) {
        console.debug('Attempting to hangup the current call.');

        if (!data.call) {
          console.debug('There is no call to hangup.');
          return false;
        }

        data.call.hangup();

        if (data.conf) {
          data.conf.destroy();
          data.conf = null;
        }

        console.debug('The call was hangup.');

        if (angular.isFunction(callback)) {
          callback(data.instance, true);
        }
      },

      /**
       * Send a DTMF to the current call.
       *
       * @param {string|integer} number
       * @param callback
       */
      dtmf: function(number, callback) {
        console.debug('Attempting to send DTMF "' + number + '".');

        if (!data.call) {
          console.debug('There is no call to send DTMF.');
          return false;
        }

        data.call.dtmf(number);
        console.debug('The DTMF was sent for the call.');

        if (angular.isFunction(callback)) {
          callback(data.instance, true);
        }
      },

      /**
       * Mute the microphone for the current call.
       *
       * @param callback
       */
      muteMic: function(callback) {
        console.debug('Attempting to mute mic for the current call.');

        if (!data.call) {
          console.debug('There is no call to mute.');
          return false;
        }

        data.call.dtmf('0');
        data.mutedMic = !data.mutedMic;
        console.debug('The mic was muted for the call.');

        if (angular.isFunction(callback)) {
          callback(data.instance, true);
        }
      },

      /**
       * Mute the video for the current call.
       *
       * @param callback
       */
      muteVideo: function(callback) {
        console.debug('Attempting to mute video for the current call.');

        if (!data.call) {
          console.debug('There is no call to mute.');
          return false;
        }

        data.call.dtmf('*0');
        data.mutedVideo = !data.mutedVideo;
        console.debug('The video was muted for the call.');

        if (angular.isFunction(callback)) {
          callback(data.instance, true);
        }
      },
      /*
      * Method is used to send conference chats ONLY.
      */
      sendConferenceChat: function(message) {
        data.conf.sendChat(message, "message");
      },
      /*
      * Method is used to send user2user chats.
      * VC does not yet support that.
      */
      sendMessage: function(body, callback) {
        data.call.message({
          to: data.chattingWith,
          body: body,
          from_msg_name: data.name,
          from_msg_number: data.cid
        });

        if (angular.isFunction(callback)) {
          callback(data.instance, true);
        }
      }
    };
  }
]);
