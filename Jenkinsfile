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
                                sh "CI=true npm test src/test/ACCESS_CARTA_DEFAULT.test.ts"
                                sh "CI=true npm test src/test/ACCESS_CARTA_KNOWN_SESSION.test.ts"
                                sh "CI=true npm test src/test/ACCESS_CARTA_NO_CLIENT_FEATURE.test.ts"
                                sh "CI=true npm test src/test/ACCESS_CARTA_SAME_ID_TWICE.test.ts"
                                sh "CI=true npm test src/test/ACCESS_CARTA_DEFAULT_CONCURRENT.test.ts"
                                sh "CI=true npm test src/test/ACCESS_WEBSOCKET.test.ts"
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
                                sh "CI=true npm test src/test/ACCESS_CARTA_DEFAULT.test.ts"
                                sh "CI=true npm test src/test/ACCESS_CARTA_KNOWN_SESSION.test.ts"
                                sh "CI=true npm test src/test/ACCESS_CARTA_NO_CLIENT_FEATURE.test.ts"
                                sh "CI=true npm test src/test/ACCESS_CARTA_SAME_ID_TWICE.test.ts"
                                sh "CI=true npm test src/test/ACCESS_CARTA_DEFAULT_CONCURRENT.test.ts"
                                sh "CI=true npm test src/test/ACCESS_WEBSOCKET.test.ts"
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
                                sh "CI=true npm test src/test/ACCESS_CARTA_DEFAULT.test.ts"
                                sh "CI=true npm test src/test/ACCESS_CARTA_KNOWN_SESSION.test.ts"
                                sh "CI=true npm test src/test/ACCESS_CARTA_NO_CLIENT_FEATURE.test.ts"
                                sh "CI=true npm test src/test/ACCESS_CARTA_SAME_ID_TWICE.test.ts"
                                sh "CI=true npm test src/test/ACCESS_CARTA_DEFAULT_CONCURRENT.test.ts"
                                sh "CI=true npm test src/test/ACCESS_WEBSOCKET.test.ts"
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
                                sh "CI=true npm test src/test/GET_FILELIST.test.ts"
                                sh "CI=true npm test src/test/GET_FILELIST_ROOTPATH_CONCURRENT.test.ts"
                                sh "CI=true npm test src/test/FILETYPE_PARSER.test.ts"
                                sh "CI=true npm test src/test/FILEINFO_FITS.test.ts"
                                sh "CI=true npm test src/test/FILEINFO_CASA.test.ts"
                                sh "CI=true npm test src/test/FILEINFO_HDF5.test.ts"
                                sh "CI=true npm test src/test/FILEINFO_MIRIAD.test.ts"
                                sh "CI=true npm test src/test/FILEINFO_FITS_MULTIHDU.test.ts"
                                sh "CI=true npm test src/test/FILEINFO_EXCEPTIONS.test.ts"
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
                                sh "CI=true npm test src/test/GET_FILELIST.test.ts"
                                sh "CI=true npm test src/test/GET_FILELIST_ROOTPATH_CONCURRENT.test.ts"
                                sh "CI=true npm test src/test/FILETYPE_PARSER.test.ts"
                                sh "CI=true npm test src/test/FILEINFO_FITS.test.ts"
                                sh "CI=true npm test src/test/FILEINFO_CASA.test.ts"
                                sh "CI=true npm test src/test/FILEINFO_HDF5.test.ts"
                                sh "CI=true npm test src/test/FILEINFO_MIRIAD.test.ts"
                                sh "CI=true npm test src/test/FILEINFO_FITS_MULTIHDU.test.ts"
                                sh "CI=true npm test src/test/FILEINFO_EXCEPTIONS.test.ts"
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
                                sh "CI=true npm test src/test/GET_FILELIST.test.ts"
                                sh "CI=true npm test src/test/GET_FILELIST_ROOTPATH_CONCURRENT.test.ts"
                                sh "CI=true npm test src/test/FILETYPE_PARSER.test.ts"
                                sh "CI=true npm test src/test/FILEINFO_FITS.test.ts"
                                sh "CI=true npm test src/test/FILEINFO_CASA.test.ts"
                                sh "CI=true npm test src/test/FILEINFO_HDF5.test.ts"
                                sh "CI=true npm test src/test/FILEINFO_MIRIAD.test.ts"
                                sh "CI=true npm test src/test/FILEINFO_FITS_MULTIHDU.test.ts"
                                sh "CI=true npm test src/test/FILEINFO_EXCEPTIONS.test.ts"
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
                                sh "CI=true npm test src/test/ANIMATOR_DATA_STREAM.test.ts"
                                sh "CI=true npm test src/test/ANIMATOR_NAVIGATION.test.ts"
                                sh "CI=true npm test src/test/ANIMATOR_PLAYBACK.test.ts"
                                sh "CI=true npm test src/test/ANIMATOR_CONTOUR_MATCH.test.ts"
                                sh "CI=true npm test src/test/ANIMATOR_CONTOUR.test.ts"
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
                                sh "CI=true npm test src/test/ANIMATOR_DATA_STREAM.test.ts"
                                sh "CI=true npm test src/test/ANIMATOR_NAVIGATION.test.ts"
                                sh "CI=true npm test src/test/ANIMATOR_PLAYBACK.test.ts"
                                sh "CI=true npm test src/test/ANIMATOR_CONTOUR_MATCH.test.ts"
                                sh "CI=true npm test src/test/ANIMATOR_CONTOUR.test.ts"
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
                                sh "CI=true npm test src/test/ANIMATOR_DATA_STREAM.test.ts"
                                sh "CI=true npm test src/test/ANIMATOR_NAVIGATION.test.ts"
                                sh "CI=true npm test src/test/ANIMATOR_PLAYBACK.test.ts"
                                sh "CI=true npm test src/test/ANIMATOR_CONTOUR_MATCH.test.ts"
                                sh "CI=true npm test src/test/ANIMATOR_CONTOUR.test.ts"
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
                                sh "CI=true npm test src/test/CONTOUR_IMAGE_DATA.test.ts"
                                sh "CI=true npm test src/test/CONTOUR_IMAGE_DATA_NAN.test.ts"
                                sh "CI=true npm test src/test/CONTOUR_DATA_STREAM.test.ts"
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
                                sh "CI=true npm test src/test/CONTOUR_IMAGE_DATA.test.ts"
                                sh "CI=true npm test src/test/CONTOUR_IMAGE_DATA_NAN.test.ts"
                                sh "CI=true npm test src/test/CONTOUR_DATA_STREAM.test.ts"
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
                                sh "CI=true npm test src/test/CONTOUR_IMAGE_DATA.test.ts"
                                sh "CI=true npm test src/test/CONTOUR_IMAGE_DATA_NAN.test.ts"
                                sh "CI=true npm test src/test/CONTOUR_DATA_STREAM.test.ts"
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
                                sh "CI=true npm test src/test/REGION_STATISTICS_RECTANGLE.test.ts"
                                sh "CI=true npm test src/test/REGION_STATISTICS_ELLIPSE.test.ts"
                                sh "CI=true npm test src/test/REGION_STATISTICS_POLYGON.test.ts"
                                sh "CI=true npm test src/test/REGION_REGISTER.test.ts"
                                sh "CI=true npm test src/test/CASA_REGION_INFO.test.ts"
                                sh "CI=true npm test src/test/CASA_REGION_IMPORT_INTERNAL.test.ts"
                                sh "CI=true npm test src/test/CASA_REGION_IMPORT_EXPORT.test.ts"
                                sh "CI=true npm test src/test/CASA_REGION_IMPORT_EXCEPTION.test.ts"
                                sh "CI=true npm test src/test/CASA_REGION_EXPORT.test.ts"
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
                                sh "CI=true npm test src/test/REGION_STATISTICS_RECTANGLE.test.ts"
                                sh "CI=true npm test src/test/REGION_STATISTICS_ELLIPSE.test.ts"
                                sh "CI=true npm test src/test/REGION_STATISTICS_POLYGON.test.ts"
                                sh "CI=true npm test src/test/REGION_REGISTER.test.ts"
                                sh "CI=true npm test src/test/CASA_REGION_INFO.test.ts"
                                sh "CI=true npm test src/test/CASA_REGION_IMPORT_INTERNAL.test.ts"
                                sh "CI=true npm test src/test/CASA_REGION_IMPORT_EXPORT.test.ts"
                                sh "CI=true npm test src/test/CASA_REGION_IMPORT_EXCEPTION.test.ts"
                                sh "CI=true npm test src/test/CASA_REGION_EXPORT.test.ts"
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
                                sh "CI=true npm test src/test/REGION_STATISTICS_RECTANGLE.test.ts"
                                sh "CI=true npm test src/test/REGION_STATISTICS_ELLIPSE.test.ts"
                                sh "CI=true npm test src/test/REGION_STATISTICS_POLYGON.test.ts"
                                sh "CI=true npm test src/test/REGION_REGISTER.test.ts"
                                sh "CI=true npm test src/test/CASA_REGION_INFO.test.ts"
                                sh "CI=true npm test src/test/CASA_REGION_IMPORT_INTERNAL.test.ts"
                                sh "CI=true npm test src/test/CASA_REGION_IMPORT_EXPORT.test.ts"
                                sh "CI=true npm test src/test/CASA_REGION_IMPORT_EXCEPTION.test.ts"
                                sh "CI=true npm test src/test/CASA_REGION_EXPORT.test.ts"
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
                                sh "CI=true npm test src/test/REGION_REGISTER.test.ts"
                                sh "CI=true npm test src/test/CASA_REGION_INFO.test.ts"
                                sh "CI=true npm test src/test/CASA_REGION_IMPORT_INTERNAL.test.ts"
                                sh "CI=true npm test src/test/CASA_REGION_IMPORT_EXPORT.test.ts"
                                sh "CI=true npm test src/test/CASA_REGION_IMPORT_EXCEPTION.test.ts"
                                sh "CI=true npm test src/test/CASA_REGION_EXPORT.test.ts"
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
                                sh "CI=true npm test src/test/REGION_REGISTER.test.ts"
                                sh "CI=true npm test src/test/CASA_REGION_INFO.test.ts"
                                sh "CI=true npm test src/test/CASA_REGION_IMPORT_INTERNAL.test.ts"
                                sh "CI=true npm test src/test/CASA_REGION_IMPORT_EXPORT.test.ts"
                                sh "CI=true npm test src/test/CASA_REGION_IMPORT_EXCEPTION.test.ts"
                                sh "CI=true npm test src/test/CASA_REGION_EXPORT.test.ts"
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
                                sh "CI=true npm test src/test/REGION_REGISTER.test.ts"
                                sh "CI=true npm test src/test/CASA_REGION_INFO.test.ts"
                                sh "CI=true npm test src/test/CASA_REGION_IMPORT_INTERNAL.test.ts"
                                sh "CI=true npm test src/test/CASA_REGION_IMPORT_EXPORT.test.ts"
                                sh "CI=true npm test src/test/CASA_REGION_IMPORT_EXCEPTION.test.ts"
                                sh "CI=true npm test src/test/CASA_REGION_EXPORT.test.ts"
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
                                sh "CI=true npm test src/test/PER_CUBE_HISTOGRAM.test.ts"
                                sh "CI=true npm test src/test/PER_CUBE_HISTOGRAM_HDF5.test.ts"
                                sh "CI=true npm test src/test/PER_CUBE_HISTOGRAM_CANCELLATION.test.ts"
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
                                sh "CI=true npm test src/test/PER_CUBE_HISTOGRAM.test.ts"
                                sh "CI=true npm test src/test/PER_CUBE_HISTOGRAM_HDF5.test.ts"
                                sh "CI=true npm test src/test/PER_CUBE_HISTOGRAM_CANCELLATION.test.ts"
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
                                sh "CI=true npm test src/test/PER_CUBE_HISTOGRAM.test.ts"
                                sh "CI=true npm test src/test/PER_CUBE_HISTOGRAM_HDF5.test.ts"
                                sh "CI=true npm test src/test/PER_CUBE_HISTOGRAM_CANCELLATION.test.ts"
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
                                sh "CI=true npm test src/test/CURSOR_SPATIAL_PROFILE.test.ts"
                                sh "CI=true npm test src/test/CURSOR_SPATIAL_PROFILE_NaN.test.ts"
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
                                sh "CI=true npm test src/test/CURSOR_SPATIAL_PROFILE.test.ts"
                                sh "CI=true npm test src/test/CURSOR_SPATIAL_PROFILE_NaN.test.ts"
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
                                sh "CI=true npm test src/test/CURSOR_SPATIAL_PROFILE.test.ts"
                                sh "CI=true npm test src/test/CURSOR_SPATIAL_PROFILE_NaN.test.ts"
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
                                sh "CI=true npm test src/test/CHECK_RASTER_TILE_DATA.test.ts"
                                sh "CI=true npm test src/test/TILE_DATA_REQUEST.test.ts"
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
                                sh "CI=true npm test src/test/CHECK_RASTER_TILE_DATA.test.ts"
                                sh "CI=true npm test src/test/TILE_DATA_REQUEST.test.ts"
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
                                sh "CI=true npm test src/test/CHECK_RASTER_TILE_DATA.test.ts"
                                sh "CI=true npm test src/test/TILE_DATA_REQUEST.test.ts"
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
                                sh "CI=true npm test src/test/SPECTRAL_LINE_QUERY.test.ts"
                                sh "CI=true npm test src/test/SPECTRAL_LINE_QUERY_INTENSITY_LIMIT.test.ts"
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
                                sh "CI=true npm test src/test/SPECTRAL_LINE_QUERY.test.ts"
                                sh "CI=true npm test src/test/SPECTRAL_LINE_QUERY_INTENSITY_LIMIT.test.ts" 
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
                                sh "CI=true npm test src/test/SPECTRAL_LINE_QUERY.test.ts"
                                sh "CI=true npm test src/test/SPECTRAL_LINE_QUERY_INTENSITY_LIMIT.test.ts" 
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
                            timeout(time: 5, unit: 'MINUTES') {
                                retry(3) {
                                    dir ('carta-backend-ICD-test') {
                                        sh "CI=true npm test src/test/MOMENTS_GENERATOR_CANCEL.test.ts"
                                        sh "CI=true npm test src/test/MOMENTS_GENERATOR_CASA.test.ts"
                                        sh "CI=true npm test src/test/MOMENTS_GENERATOR_EXCEPTION.test.ts"
                                        sh "CI=true npm test src/test/MOMENTS_GENERATOR_FITS.test.ts"
                                        sh "CI=true npm test src/test/MOMENTS_GENERATOR_HDF5.test.ts"
                                        sh "CI=true npm test src/test/MOMENTS_GENERATOR_PROFILE_STREAM.test.ts"
                                        sh "CI=true npm test src/test/MOMENTS_GENERATOR_SAVE.test.ts"
                                    }
                                }
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
                                sh "CI=true npm test src/test/MOMENTS_GENERATOR_CANCEL.test.ts"
                                sh "CI=true npm test src/test/MOMENTS_GENERATOR_CASA.test.ts"
                                sh "CI=true npm test src/test/MOMENTS_GENERATOR_EXCEPTION.test.ts"
                                sh "CI=true npm test src/test/MOMENTS_GENERATOR_FITS.test.ts"
                                sh "CI=true npm test src/test/MOMENTS_GENERATOR_HDF5.test.ts"
                                sh "CI=true npm test src/test/MOMENTS_GENERATOR_PROFILE_STREAM.test.ts"
                                sh "CI=true npm test src/test/MOMENTS_GENERATOR_SAVE.test.ts"
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
                                sh "CI=true npm test src/test/MOMENTS_GENERATOR_CANCEL.test.ts"
                                sh "CI=true npm test src/test/MOMENTS_GENERATOR_CASA.test.ts"
                                sh "CI=true npm test src/test/MOMENTS_GENERATOR_EXCEPTION.test.ts"
                                sh "CI=true npm test src/test/MOMENTS_GENERATOR_FITS.test.ts"
                                sh "CI=true npm test src/test/MOMENTS_GENERATOR_HDF5.test.ts"
                                sh "CI=true npm test src/test/MOMENTS_GENERATOR_PROFILE_STREAM.test.ts"
                                sh "CI=true npm test src/test/MOMENTS_GENERATOR_SAVE.test.ts"
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
                                sh "CI=true npm test src/test/RESUME_CATALOG.test.ts"
                                sh "CI=true npm test src/test/RESUME_CONTOUR.test.ts"
                                sh "CI=true npm test src/test/RESUME_IMAGE.test.ts"
                                sh "CI=true npm test src/test/RESUME_REGION.test.ts"
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
                                sh "CI=true npm test src/test/RESUME_CATALOG.test.ts"
                                sh "CI=true npm test src/test/RESUME_CONTOUR.test.ts"
                                sh "CI=true npm test src/test/RESUME_IMAGE.test.ts"
                                sh "CI=true npm test src/test/RESUME_REGION.test.ts"
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
                                sh "CI=true npm test src/test/RESUME_CATALOG.test.ts"
                                sh "CI=true npm test src/test/RESUME_CONTOUR.test.ts"
                                sh "CI=true npm test src/test/RESUME_IMAGE.test.ts"
                                sh "CI=true npm test src/test/RESUME_REGION.test.ts"
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

