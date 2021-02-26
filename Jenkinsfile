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
        stage('Build') {
            parallel {
                stage('CentOS7 build') {
                    agent {
                        label "centos7-1"
                    }
                    steps {
                        sh "export PATH=/usr/local/bin:$PATH"
                        sh "git submodule update --init --recursive"
                        dir ('build') {
                           sh "rm -rf *"
                           sh "cp ../../cmake-command.sh ."
                           sh "./cmake-command.sh"
                           sh "make -j\$(nproc)"
                           echo "Preparing for upcoming ICD tests"
                           sh "rm -rf carta-backend-ICD-test"
                           sh "git clone https://github.com/CARTAvis/carta-backend-ICD-test.git && cp ../../run.sh ."
                           dir ('carta-backend-ICD-test') {
                              sh "git submodule init && git submodule update && npm install"
                              dir ('protobuf') {
                                 sh "./build_proto.sh"
                              }
                              sh "cp ../../../ws-config.json src/test/config.json"
                           }
                           stash includes: "carta_backend", name: "centos7-1_carta_backend_icd"
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
                        sh "git submodule update --init --recursive"
                        dir ('build') {
                           sh "rm -rf *"
                           sh "cp ../../cmake-command.sh ."
                           sh "./cmake-command.sh"
                           sh "make -j\$(nproc)"
                           echo "Preparing for upcoming ICD tests"
                           sh "rm -rf carta-backend-ICD-test"
                           sh "git clone https://github.com/CARTAvis/carta-backend-ICD-test.git && cp ../../run.sh ."
                           dir ('carta-backend-ICD-test') {
                              sh "git submodule init && git submodule update && npm install"
                              dir ('protobuf') {
                                 sh "./build_proto.sh"
                              }
                              sh "cp ../../../ws-config.json src/test/config.json"
                           }
                           stash includes: "carta_backend", name: "macos-1_carta_backend_icd"
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
                        sh "git submodule update --init --recursive"
                        dir ('build') {
                           sh "rm -rf *"
                           sh "cp ../../cmake-command.sh ."
                           sh "./cmake-command.sh"
                           sh "make -j\$(nproc)"
                           echo "Preparing for upcoming ICD tests"
                           sh "rm -rf carta-backend-ICD-test"
                           sh "git clone https://github.com/CARTAvis/carta-backend-ICD-test.git && cp ../../run.sh ."
                           dir ('carta-backend-ICD-test') {
                              sh "git submodule init && git submodule update && npm install"
                              dir ('protobuf') {
                                 sh "./build_proto.sh"
                              }
                              sh "cp ../../../ws-config.json src/test/config.json"
                         }
                         stash includes: "carta_backend", name: "ubuntu-1_carta_backend_icd"
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
        stage('ICD tests: session') {
            matrix {
                agent any
                axes {
                    axis {
                        name 'PLATFORM'
                        values 'centos7-1', 'ubuntu-1', 'macos-1'
                    }
                }
                stages {
                    stage('${PLATFORM}') {
                        agent {
                            label "${PLATFORM}"
                        }
                        steps {
                            catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                            {
                            sh "export PATH=/usr/local/bin:$PATH"
                            dir ('build') {
                                unstash "${PLATFORM}_carta_backend_icd"
                                sh "./run.sh # run carta_backend in the background"
                                dir ('carta-backend-ICD-test') {
                                    sh "CI=true npm test src/test/ACCESS_CARTA_DEFAULT.test.ts # test 1 of 6"
                                    sh "CI=true npm test src/test/ACCESS_CARTA_KNOWN_SESSION.test.ts # test 2 of 6"
                                    sh "CI=true npm test src/test/ACCESS_CARTA_NO_CLIENT_FEATURE.test.ts # test 3 of 6"
                                    sh "CI=true npm test src/test/ACCESS_CARTA_SAME_ID_TWICE.test.ts # test 4 of 6"
                                    sh "CI=true npm test src/test/ACCESS_CARTA_DEFAULT_CONCURRENT.test.ts # test 5 of 6"
                                    sh "CI=true npm test src/test/ACCESS_WEBSOCKET.test.ts # test 6 of 6"
                                }
                            }
                            echo "Finished !!"
                            }
                        }
                    }
                }
            }
        }
    }
}

