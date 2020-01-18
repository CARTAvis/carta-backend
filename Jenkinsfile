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
                stage('CentOS7 ICD') {
                    agent {
                        label "centos7-1"
                    }
                    steps {
                        sh "ls"
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
                 stage('MacOS ICD') {
                     agent {
                         label "macos-1"
                     }
                     steps {
                         sh "ls"
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
              }
          }
     } 
}
