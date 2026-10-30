#ifndef CA_CERTIFICATES_H
#define CA_CERTIFICATES_H

/**
 * @brief Point the embedded OpenSSL to the system CA certificates.
 *
 * OpenSSL is built with the /usr/local prefix, so it looks for its root
 * certificates in /usr/local/ssl. On Android that directory does not exist and
 * every verified TLS connection fails ("certificate verify failed"). Android's
 * own certificate directory can't be used as an OpenSSL CA path either: its
 * files are named with the old subject hash (subject_hash_old), that OpenSSL 1.1+
 * doesn't look up. The certificates are concatenated into a single PEM bundle
 * instead, and SSL_CERT_FILE points to it (OpenSSL's default store reads it).
 *
 * Does nothing outside Android, or when SSL_CERT_FILE is already set.
 *
 * @param baseDirectory writable directory where the bundle is written
 * @return 0 on success or when there is nothing to do, -1 on error
 */
int ca_certificates_setup(const char* baseDirectory);

/**
 * @brief Concatenate every certificate file of a directory into a PEM bundle.
 *
 * The bundle is rebuilt only when the directory changed after it was written
 * (system updates replace the certificates), through a temporary file so that a
 * failure never leaves a truncated bundle.
 *
 * @param certificatesDirectory directory with one PEM certificate per file
 * @param bundlePath path of the bundle to write
 * @return 0 on success, -1 on error
 */
int ca_certificates_write_bundle(const char* certificatesDirectory, const char* bundlePath);

#endif //CA_CERTIFICATES_H
