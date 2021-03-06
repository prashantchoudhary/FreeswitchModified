(function() {
  'use strict';

  angular
    .module('vertoControllers')
    .controller('MainController',
      function($scope, $rootScope, $location, $modal, $timeout, verto, storage, CallHistory, toastr, Fullscreen, prompt) {

      console.debug('Executing MainController.');

      var myVideo = document.getElementById("webcam");
      $scope.verto = verto;
      $scope.storage = storage;
      $scope.call_history = angular.element("#call_history").hasClass('active');
      $scope.chatStatus = angular.element('#wrapper').hasClass('toggled');

      /**
       * (explanation) scope in another controller extends rootScope (singleton)
       */
      $rootScope.chat_counter = 0;
      $rootScope.activePane = 'members';
      /**
       * The number that will be called.
       * @type {string}
       */
      $rootScope.dialpadNumber = '';


      /**
       * if user data saved, use stored data for logon
       */
      if (storage.data.ui_connected && storage.data.ws_connected) {
        $scope.verto.data.name = storage.data.name;
        $scope.verto.data.email = storage.data.email;
        $scope.verto.data.login = storage.data.login;
        $scope.verto.data.password = storage.data.password;

        verto.connect(function(v, connected) {
          $scope.$apply(function() {
            if (connected) {
              toastr.success('Nice to see you again.', 'Welcome back');
              $location.path('/dialpad');
            }
          });
        });

      }

      // If verto is not connected, redirects to login page.
      if (!verto.data.connected) {
        console.debug('MainController: WebSocket not connected. Redirecting to login.');
        $location.path('/login');
      }

      /**
       * Login the user to verto server and
       * redirects him to dialpad page.
       */
      $scope.login = function() {
        var connectCallback = function(v, connected) {
          $scope.$apply(function() {
            if (connected) {
              storage.data.ui_connected = verto.data.connected;
              storage.data.ws_connected = verto.data.connected;
              storage.data.name = verto.data.name;
              storage.data.email = verto.data.email;
              storage.data.login = verto.data.login;
              storage.data.password = verto.data.password;

              console.debug('Redirecting to dialpad page.');
              toastr.success('Login successful.', 'Welcome');
              $location.path('/dialpad');
            } else {
              toastr.error('There was an error while trying to login. Please try again.', 'Error');
            }
          });
        };

        verto.connect(connectCallback);
      };

      /**
       * Logout the user from verto server and
       * redirects him to login page.
       */
      $scope.logout = function() {
        var disconnect = function() {
          var disconnectCallback = function(v, connected) {
            console.debug('Redirecting to login page.');
            storage.reset();
            $location.path('/login');
          };

          if (verto.data.call) {
            verto.hangup();
          }

          $scope.closeChat();
          verto.disconnect(disconnectCallback);

          verto.hangup();
        };

        if (verto.data.call) {
          prompt({
            title: 'Oops, Active Call in Course.',
            message: 'It seems that you are in a call. Do you want to hang up?'
          }).then(function() {
            disconnect();
          });
        } else {
          disconnect();
        }

      };

      /**
       * Shows a modal with the settings.
       */
      $scope.openModalSettings = function() {
        var modalInstance = $modal.open({
          animation: $scope.animationsEnabled,
          templateUrl: 'partials/modal_settings.html',
          controller: 'ModalSettingsController',
        });

        modalInstance.result.then(
          function(result) {
            console.log(result);
          },
          function() {
            console.info('Modal dismissed at: ' + new Date());
          }
        );

        modalInstance.rendered.then(
          function() {
            jQuery.material.init();
          }
        );
      };

      $rootScope.openModal = function(templateUrl, controller) {
        var modalInstance = $modal.open({
          animation: $scope.animationsEnabled,
          templateUrl: templateUrl,
          controller: controller,
        });

        modalInstance.result.then(
          function(result) {
            console.log(result);
          },
          function() {
            console.info('Modal dismissed at: ' + new Date());
          }
        );

        modalInstance.rendered.then(
          function() {
            jQuery.material.init();
          }
        );

      };

      $scope.showContributors = function() {
        $scope.openModal('partials/contributors.html', 'ContributorsController');
      };

      /**
       * Updates the display adding the new number touched.
       *
       * @param {String} number - New touched number.
       */
      $rootScope.dtmf = function(number) {
        $rootScope.dialpadNumber = $scope.dialpadNumber + number;
        if (verto.data.call) {
          verto.dtmf(number);
        }
      };

      /**
       * Removes the last character from the number.
       */
      $rootScope.backspace = function() {
        var number = $rootScope.dialpadNumber;
        var len = number.length;
        $rootScope.dialpadNumber = number.substring(0, len - 1);
      };


      $scope.toggleCallHistory = function() {
        if (!$scope.call_history) {
          angular.element("#call_history").addClass('active');
          angular.element("#call-history-wrapper").addClass('active');
        } else {
          angular.element("#call_history").removeClass('active');
          angular.element("#call-history-wrapper").removeClass('active');
        }
        $scope.call_history = angular.element("#call_history").hasClass('active');
      };

      $scope.toggleChat = function() {
        if ($scope.chatStatus && $rootScope.activePane === 'chat') {
          $rootScope.chat_counter = 0;
        }
        angular.element('#wrapper').toggleClass('toggled');
        $scope.chatStatus = angular.element('#wrapper').hasClass('toggled');
      };

      $scope.openChat = function() {
        $scope.chatStatus = false;
        angular.element('#wrapper').removeClass('toggled');
      };

      $scope.closeChat = function() {
        $scope.chatStatus = true;
        angular.element('#wrapper').addClass('toggled');
      };

      $scope.goFullscreen = function() {
        if (storage.data.userStatus !== 'connected') {
          return;
        }
        $rootScope.fullscreenEnabled = !Fullscreen.isEnabled();
        if (Fullscreen.isEnabled()) {
          Fullscreen.cancel();
        } else {
          Fullscreen.enable(document.getElementsByTagName('body')[0]);
        }
      };

      $rootScope.$on('call.video', function(event) {
        storage.data.videoCall = true;
      });

      $rootScope.$on('call.hangup', function(event, data) {
        if (Fullscreen.isEnabled()) {
          Fullscreen.cancel();
        }


        console.log($scope.chatStatus);
        if (!$scope.chatStatus) {
          angular.element('#wrapper').toggleClass('toggled');
          $scope.chatStatus = angular.element('#wrapper').hasClass('toggled');
        }

        $rootScope.dialpadNumber = '';
        console.debug('Redirecting to dialpad page.');
        $location.path('/dialpad');

        try {
          $rootScope.$digest();
        } catch (e) {
          console.log('not digest');
        }
      });

      $rootScope.$on('page.incall', function(event, data) {
        if (storage.data.askRecoverCall) {
          prompt({
            title: 'Oops, Active Call in Course.',
            message: 'It seems you were in a call before leaving the last time. Wanna go back to that?'
          }).then(function() {
            console.log('redirect to incall page');
            $location.path('/incall');
          }, function() {
            storage.data.userStatus = 'connecting';
            verto.hangup();
          });
        } else {
          console.log('redirect to incall page');
          $location.path('/incall');
        }

      });

      $rootScope.callActive = function(data) {
        verto.data.mutedMic = storage.data.mutedMic;
        verto.data.mutedVideo = storage.data.mutedVideo;

        if (!storage.data.cur_call) {
          storage.data.call_start = new Date();
        }
        storage.data.userStatus = 'connected';
        var call_start = new Date(storage.data.call_start);
        $rootScope.start_time = call_start;

        $timeout(function() {
          $scope.$broadcast('timer-start');
        });
        myVideo.play();
        storage.data.calling = false;

        storage.data.cur_call = 1;
      };

      $rootScope.$on('call.active', function(event, data) {
        $rootScope.callActive(data);
      });

      $rootScope.$on('call.calling', function(event, data) {
        storage.data.calling = true;
      });

      $rootScope.$on('call.incoming', function(event, data) {
        console.log('Incoming call from: ' + data);

        storage.data.cur_call = 0;
        $scope.incomingCall = true;
        storage.data.videoCall = false;
        storage.data.mutedVideo = false;
        storage.data.mutedMic = false;

        prompt({
          title: 'Incoming Call',
          message: 'from ' + data
        }).then(function() {
          var call_start = new Date(storage.data.call_start);
          $rootScope.start_time = call_start;
          console.log($rootScope.start_time);

          $scope.answerCall();
          storage.data.called_number = data;
          CallHistory.add(number, 'inbound', true);
          $location.path('/incall');
        }, function() {
          $scope.declineCall();
          CallHistory.add(number, 'inbound', false);
        });
      });

      $scope.hold = function() {
        storage.data.onHold = !storage.data.onHold;
        verto.data.call.toggleHold();
      };

      /**
       * Hangup the current call.
       */
      $scope.hangup = function() {
        if (!verto.data.call) {
          toastr.warning('There is no call to hangup.');
          $location.path('/dialpad');
        }

        //var hangupCallback = function(v, hangup) {
        //  if (hangup) {
        //    $location.path('/dialpad');
        //  } else {
        //    console.debug('The call could not be hangup.');
        //  }
        //};
        //
        //verto.hangup(hangupCallback);

        verto.hangup();
      };

      $scope.answerCall = function() {
        storage.data.onHold = false;

        verto.data.call.answer({
          useStereo: storage.data.useStereo,
          useCamera: storage.data.selectedVideo,
          useMic: storage.data.useMic,
          callee_id_name: verto.data.name,
          callee_id_number: verto.data.login
        });


        $location.path('/incall');
      };

      $scope.declineCall = function() {
        $scope.hangup();
        $scope.incomingCall = false;
      };

      $scope.screenshare = function() {
        if (verto.data.shareCall) {
          verto.screenshareHangup();
          return false;
        }
        verto.screenshare(storage.data.called_number);
      };

      $scope.play = function() {
        var file = $scope.promptInput('Please, enter filename', '', 'File',
          function(file) {
            verto.data.conf.play(file);
            console.log('play file :', file);
          });

      };

      $scope.stop = function() {
        verto.data.conf.stop();
      };

      $scope.record = function() {
        var file = $scope.promptInput('Please, enter filename', '', 'File',
          function(file) {
            verto.data.conf.record(file);
            console.log('recording file :', file);
          });
      };

      $scope.stopRecord = function() {
        verto.data.conf.stopRecord();
      };

      $scope.snapshot = function() {
        var file = $scope.promptInput('Please, enter filename', '', 'File',
          function(file) {
            verto.data.conf.snapshot(file);
            console.log('snapshot file :', file);
          });
      };


    }
  );

})();
