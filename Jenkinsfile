void setBuildStatus(String message, String state) {
  step([
      $class: "GitHubCommitStatusSetter",
      reposSource: [$class: "ManuallyEnteredRepositorySource", url: "https://github.com/CARTAvis/carta-backend"],
      contextSource: [$class: "ManuallyEnteredCommitContextSource", context: "ci/jenkins/build-status"],
      errorHandlers: [[$class: "ChangingBuildStatusErrorHandler", result: "UNSTABLE"]],
      statusResultSource: [ $class: "ConditionalStatusResultSource", results: [[$class: "AnyBuildResult", message: message, state: state]] ]
  ]);
}

pipeline {
    agent none
    stages {
        stage('build backend') {
            parallel {
                stage('CentOS7 build') {
                    agent {
                        label "centos7-1"
                    }
                    steps {
                        sh "export PATH=/usr/local/bin:$PATH"
                        sh "git submodule init && git submodule update"
                        dir ('build') {
                        sh "cp ../../cmake-command.sh ."
                        sh "./cmake-command.sh"
                        sh "make"
                        stash includes: "carta_backend", name: "centos7-1_carta_backend"
                        }
                    }
                    post {
                        success {
                            setBuildStatus("CentOS7 build succeeded", "SUCCESS");
                        }
                        failure {
                            setBuildStatus("CentOS7 build failed", "FAILURE");
                        }
                    }
                }
                stage('MacOS build') {
                    agent {
                        label "macos-1"
                    }
                    steps {
                        sh "export PATH=/usr/local/bin:$PATH"
                        sh "git submodule init && git submodule update"
                        dir ('build') {
                        sh "cp ../../cmake-command.sh ."
                        sh "./cmake-command.sh"
                        sh "make"
                        stash includes: "carta_backend", name: "macos-1_carta_backend"
                        }
                    }
                    post {
                        success {
                            setBuildStatus("MacOS build succeeded", "SUCCESS");
                        }
                        failure {
                            setBuildStatus("MacOS build failed", "FAILURE");
                        }
                    }
                }
                stage('Ubuntu build') {
                    agent {
                        label "ubuntu-1"
                    }
                    steps {
                        sh "export PATH=/usr/local/bin:$PATH"
                        sh "git submodule init && git submodule update"
                        dir ('build') {
                        sh "cp ../../cmake-command.sh ."
                        sh "./cmake-command.sh"
                        sh "make"
                        stash includes: "carta_backend", name: "ubuntu-1_carta_backend"
                        }
                    }
                    post {
                        success {
                            setBuildStatus("MacOS build succeeded", "SUCCESS");
                        }
                        failure {
                            setBuildStatus("MacOS build failed", "FAILURE");
                        }
                    }
                }
            }
        }
        stage('ICD tests') {
            parallel {
                stage('MacOS ICD') {
                    agent {
                        label "centos7-1"
                    }
                    steps {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "centos7-1_carta_backend"
                            sh "rm -rf carta-backend-ICD-test"
                            sh "git clone https://github.com/CARTAvis/carta-backend-ICD-test.git && cp ../../run.sh ."
                            sh "./run.sh # run carta_backend in the background"
                            sh "/usr/sbin/lsof -i :3002 # check backend is running"
                            dir ('carta-backend-ICD-test') {
                                sh "git submodule init && git submodule update && npm install"
                                dir ('protobuf') {
                                     sh "./build_proto.sh"
                                     sh "pwd; ls"
                                }
                                sh "pwd; ls"
                                sh "cp ../../../config.json src/test/ && cp ../../../run-jenkins.sh . && ./run-jenkins.sh # run the tests"
                                sh "sleep 30"
                            }
                        }
                        echo "Finished !!"
                    }
                    post {
                        success {
                            setBuildStatus("MacOS ICD tests succeeded", "SUCCESS");
                        }
                        failure {
                            setBuildStatus("MacOS ICD tests failed", "FAILURE");
                        }     
                    }
                 }
                 stage('CentOS7 ICD') {
                     agent {
                         label "ubuntu-1"
                     }
                     steps {
                         sh "export PATH=/usr/local/bin:$PATH"
                         dir ('build') {
                             unstash "ubuntu-1_carta_backend"
                             sh "rm -rf carta-backend-ICD-test"
                             sh "git clone https://github.com/CARTAvis/carta-backend-ICD-test.git && cp ../../run.sh ."
                             sh "./run.sh # run carta_backend in the background"
                             dir ('carta-backend-ICD-test') {
                                 sh "git submodule init && git submodule update && npm install"
                                 dir ('protobuf') {
                                     sh "./build_proto.sh"
                                     sh "pwd; ls"
                                 }
                                 sh "pwd; ls"
                                 sh "cp ../../../config.json src/test/ && cp ../../../run-jenkins.sh . && ./run-jenkins.sh # run the tests"
                                 sh "sleep 30"
                             }
                         }
                         echo "Finished !!"
                     }
                     post {
                         success {
                             setBuildStatus("CentOS7 ICD tests succeeded", "SUCCESS");
                         }
                         failure {
                             setBuildStatus("CentOS7 ICD tests failed", "FAILURE");
                         }     
                     }
                  }
                 stage('Ubuntu ICD') {
                     agent {
                         label "macos-1"
                     }
                     steps {
                         sh "export PATH=/usr/local/bin:$PATH"
                         dir ('build') {
                             unstash "macos-1_carta_backend"
                             sh "rm -rf carta-backend-ICD-test"
                             sh "git clone https://github.com/CARTAvis/carta-backend-ICD-test.git && cp ../../run.sh ."
                             sh "./run.sh # run carta_backend in the background"
                             dir ('carta-backend-ICD-test') {
                                 sh "git submodule init && git submodule update && npm install"
                                 dir ('protobuf') {
                                     sh "./build_proto.sh"
                                     sh "pwd; ls"
                                 }
                                 sh "pwd; ls"
                                 sh "cp ../../../config.json src/test/ && cp ../../../run-jenkins.sh . && ./run-jenkins.sh # run the tests"
                                 sh "sleep 30"
                             }
                         }
                         echo "Finished !!"
                     }
                     post {
                         success {
                             setBuildStatus("Ubuntu ICD tests succeeded", "SUCCESS");
                         }
                         failure {
                             setBuildStatus("Ubuntu ICD tests failed", "FAILURE");
                         }
                     }
                  }
              }
          }
     } 
}
