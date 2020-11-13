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
                        sh "git submodule init && git submodule update"
                        dir ('build') {
                           sh "cp ../../cmake-command.sh ."
                           sh "./cmake-command.sh"
                           sh "make"
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
                        sh "git submodule init && git submodule update"
                        dir ('build') {
                           sh "cp ../../cmake-command.sh ."
                           sh "./cmake-command.sh"
                           sh "make"
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
                        sh "git submodule init && git submodule update"
                        dir ('build') {
                           sh "cp ../../cmake-command.sh ."
                           sh "./cmake-command.sh"
                           sh "make"
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
            parallel {
                stage('CentOS7') {
                    agent {
                        label "centos7-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "centos7-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                sh "cp ../../../run-jenkins-session.sh . && ./run-jenkins-session.sh # run the tests"
                            }
                        }
                        echo "Finished !!"
                        }
                    }
                }
                stage('Ubuntu') {
                    agent {
                        label "ubuntu-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "ubuntu-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                sh "cp ../../../run-jenkins-session.sh . && ./run-jenkins-session.sh # run the tests"
                            }
                        }
                        echo "Finished !!"
                        }
                    }
                }
                stage('MacOS') {
                    agent {
                        label "macos-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "macos-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                sh "cp ../../../run-jenkins-session.sh . && ./run-jenkins-session.sh # run the tests"
                            }
                        }
                        echo "Finished !!"
                        }
                    }
                }    
            }
        }
        stage('ICD tests: file-browser') {
            parallel {
                stage('CentOS7') {
                    agent {
                        label "centos7-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "centos7-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                sh "cp ../../../run-jenkins-file-browser.sh . && ./run-jenkins-file-browser.sh # run the tests"
                            }
                        }
                        echo "Finished !!"
                        }
                    }
                }
                stage('Ubuntu') {
                    agent {
                        label "ubuntu-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "ubuntu-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                sh "cp ../../../run-jenkins-file-browser.sh . && ./run-jenkins-file-browser.sh # run the tests"
                            }
                        }
                        echo "Finished !!"
                        }
                    }
                }
                stage('MacOS') {
                    agent {
                        label "macos-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "macos-1_carta_backend_icd" 
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                sh "cp ../../../run-jenkins-file-browser.sh . && ./run-jenkins-file-browser.sh # run the tests"
                            }
                        }
                        echo "Finished !!"
                        }
                    }
                }
            }
        }
        stage('ICD tests: animator') {
            parallel {
                stage('CentOS7') {
                    agent {
                        label "centos7-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "centos7-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                sh "cp ../../../run-jenkins-animator.sh . && ./run-jenkins-animator.sh # run the tests"
                            }
                        }
                        echo "Finished !!"
                        }
                    }
                }
                stage('Ubuntu') {
                    agent {
                        label "ubuntu-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "ubuntu-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                sh "cp ../../../run-jenkins-animator.sh . && ./run-jenkins-animator.sh # run the tests"
                            }
                        }
                        echo "Finished !!"
                        }
                    }
                }
                stage('MacOS') {
                    agent {
                        label "macos-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "macos-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                sh "cp ../../../run-jenkins-animator.sh . && ./run-jenkins-animator.sh # run the tests"
                            }
                        }
                        echo "Finished !!"
                        }
                   }
               }     
            }
        }
        stage('ICD tests: contour') {
            parallel {
                stage('CentOS7') {
                    agent {
                        label "centos7-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "centos7-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                sh "cp ../../../run-jenkins-contour.sh . && ./run-jenkins-contour.sh # run the tests"
                            }
                        }
                        echo "Finished !!"
                        }
                    }
                }
                stage('Ubuntu') {
                    agent {
                        label "ubuntu-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "ubuntu-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                sh "cp ../../../run-jenkins-contour.sh . && ./run-jenkins-contour.sh # run the tests"
                            }
                        }
                        echo "Finished !!"
                        }
                    }
                }
                stage('MacOS') {
                    agent {
                        label "macos-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "macos-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                sh "cp ../../../run-jenkins-contour.sh . && ./run-jenkins-contour.sh # run the tests"
                            }
                        }
                        echo "Finished !!"
                        }
                    }
                }
            }
        }
        stage('ICD tests: region statistics') {
            parallel {
                stage('CentOS7') {
                    agent {
                        label "centos7-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "centos7-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                sh "cp ../../../run-jenkins-region-statistics.sh . && ./run-jenkins-region-statistics.sh # run the tests"
                            }
                        }
                        echo "Finished !!"
                        }
                    }
                }
                stage('Ubuntu') {
                    agent {
                        label "ubuntu-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "ubuntu-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                sh "cp ../../../run-jenkins-region-statistics.sh . && ./run-jenkins-region-statistics.sh # run the tests"
                            }
                        }
                        echo "Finished !!"
                        }
                    }
                }
                stage('MacOS') {
                    agent {
                        label "macos-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "macos-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                sh "cp ../../../run-jenkins-region-statistics.sh . && ./run-jenkins-region-statistics.sh # run the tests"
                            }
                        }
                        echo "Finished !!"
                        }
                    }
                }
            }
        }
        stage('ICD tests: region manipulation') {
            parallel {
                stage('CentOS7') {
                    agent {
                        label "centos7-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "centos7-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                sh "cp ../../../run-jenkins-region-manipulation.sh . && ./run-jenkins-region-manipulation.sh # run the tests"
                            }
                        }
                        echo "Finished !!"
                        }
                    }
                }
                stage('Ubuntu') {
                    agent {
                        label "ubuntu-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "ubuntu-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                sh "cp ../../../run-jenkins-region-manipulation.sh . && ./run-jenkins-region-manipulation.sh # run the tests"
                            }
                        }
                        echo "Finished !!"
                        }
                    }
                }
                stage('MacOS') {
                    agent {
                        label "macos-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "macos-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                sh "cp ../../../run-jenkins-region-manipulation.sh . && ./run-jenkins-region-manipulation.sh # run the tests"
                            }
                        }
                        echo "Finished !!"
                        }
                    }
                }
            }
        }
        stage('ICD tests: cube histogram') {
            parallel {
                stage('CentOS7') {
                    agent {
                        label "centos7-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "centos7-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                sh "cp ../../../run-jenkins-cube.sh . && ./run-jenkins-cube.sh # run the tests"
                            }
                        }
                        echo "Finished !!"
                        }
                    }
                }
                stage('Ubuntu') {
                    agent {
                        label "ubuntu-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "ubuntu-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                sh "cp ../../../run-jenkins-cube.sh . && ./run-jenkins-cube.sh # run the tests"
                            }
                        }
                        echo "Finished !!"
                        }
                    }
                }
                stage('MacOS') {
                    agent {
                        label "macos-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "macos-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                sh "cp ../../../run-jenkins-cube.sh . && ./run-jenkins-cube.sh # run the tests"
                            }
                        }
                        echo "Finished !!"
                        }
                    }
                }
            }
        }
        stage('ICD tests: spatial profiler') {
            parallel {
                stage('CentOS7') {
                    agent {
                        label "centos7-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "centos7-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                sh "cp ../../../run-jenkins-spatial-profiler.sh . && ./run-jenkins-spatial-profiler.sh # run the tests"
                            }
                        }
                        echo "Finished !!"
                        }
                    }
                }
                stage('Ubuntu') {
                    agent {
                        label "ubuntu-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "ubuntu-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                sh "cp ../../../run-jenkins-spatial-profiler.sh . && ./run-jenkins-spatial-profiler.sh # run the tests"
                            }
                        }
                        echo "Finished !!"
                        }
                    }
                }
                stage('MacOS') {
                    agent {
                        label "macos-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "macos-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                sh "cp ../../../run-jenkins-spatial-profiler.sh . && ./run-jenkins-spatial-profiler.sh # run the tests"
                            }
                        }
                        echo "Finished !!"
                        }
                    }
                }
            }
        }
        stage('ICD tests: raster tiles') {
            parallel {
                stage('CentOS7') {
                    agent {
                        label "centos7-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "centos7-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                sh "cp ../../../run-jenkins-raster-tiles.sh . && ./run-jenkins-raster-tiles.sh # run the tests"
                            }
                        }
                        echo "Finished !!"
                        }
                    }
                }
                stage('Ubuntu') {
                    agent {
                        label "ubuntu-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "ubuntu-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                sh "cp ../../../run-jenkins-raster-tiles.sh . && ./run-jenkins-raster-tiles.sh # run the tests"
                            }
                        }
                        echo "Finished !!"
                        }
                    }
                }
                stage('MacOS') {
                    agent {
                        label "macos-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "macos-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                sh "cp ../../../run-jenkins-raster-tiles.sh . && ./run-jenkins-raster-tiles.sh # run the tests"
                            }
                        }
                        echo "Finished !!"
                        }
                    }
                }
            }
        }
        stage('ICD tests: spectral line query') {
            parallel {
                stage('CentOS7') {
                    agent {
                        label "centos7-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "centos7-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                sh "cp ../../../run-jenkins-line-query.sh . && ./run-jenkins-line-query.sh # run the tests"
                            }
                        }
                        echo "Finished !!"
                        }
                    }
                }
                stage('Ubuntu') {
                    agent {
                        label "ubuntu-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "ubuntu-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                sh "cp ../../../run-jenkins-line-query.sh . && ./run-jenkins-line-query.sh # run the tests"
                            }
                        }
                        echo "Finished !!"
                        }
                    }
                }
                stage('MacOS') {
                    agent {
                        label "macos-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "macos-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                sh "cp ../../../run-jenkins-line-query.sh . && ./run-jenkins-line-query.sh # run the tests"
                            }
                        }
                        echo "Finished !!"
                        }
                    }
                }
            }
        }
        stage('ICD tests: moments') {
            parallel {
                stage('CentOS7') {
                    agent {
                        label "centos7-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "centos7-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                sh "cp ../../../run-jenkins-moments.sh . && ./run-jenkins-moments.sh # run the tests"
                            }
                        }
                        echo "Finished !!"
                        }
                    }
                }
                stage('Ubuntu') {
                    agent {
                        label "ubuntu-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "ubuntu-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                sh "cp ../../../run-jenkins-moments.sh . && ./run-jenkins-moments.sh # run the tests"
                            }
                        }
                        echo "Finished !!"
                        }
                    }
                }
                stage('MacOS') {
                    agent {
                        label "macos-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "macos-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                sh "cp ../../../run-jenkins-moments.sh . && ./run-jenkins-moments.sh # run the tests"
                            }
                        }
                        echo "Finished !!"
                        }
                    }
                }
            }
        }
        stage('ICD tests: resume') {
            parallel {
                stage('CentOS7') {
                    agent {
                        label "centos7-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "centos7-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                sh "cp ../../../run-jenkins-resume.sh . && ./run-jenkins-resume.sh # run the tests"
                            }
                        }
                        echo "Finished !!"
                        }
                    }
                }
                stage('Ubuntu') {
                    agent {
                        label "ubuntu-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "ubuntu-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                sh "cp ../../../run-jenkins-resume.sh . && ./run-jenkins-resume.sh # run the tests"
                            }
                        }
                        echo "Finished !!"
                        }
                    }
                }
                stage('MacOS') {
                    agent {
                        label "macos-1"
                    }
                    steps {
                        catchError(buildResult: 'SUCCESS', stageResult: 'FAILURE')
                        {
                        sh "export PATH=/usr/local/bin:$PATH"
                        dir ('build') {
                            unstash "macos-1_carta_backend_icd"
                            sh "./run.sh # run carta_backend in the background"
                            dir ('carta-backend-ICD-test') {
                                sh "cp ../../../run-jenkins-resume.sh . && ./run-jenkins-resume.sh # run the tests"
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

