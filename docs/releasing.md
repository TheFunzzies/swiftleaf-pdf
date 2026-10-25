# Releasing SwiftLeaf

Push a `v*` tag to build and publish the Windows release. The workflow publishes
`Swiftleaf.exe`, `Swiftleaf-install.exe`, `update-check.txt`, and
`SHA256SUMS.txt`. The updater requires the installer SHA-256 value from
`update-check.txt` to match before it launches the installer.

## Code signing requirements

> [!WARNING]
> **Do NOT use self-signed certificates for releases.**
> Swiftleaf's in-app updater (`src/UpdateCheck.cpp` via `IsUpdateSignerValid` and
> `WinVerifyTrust`) validates that update payloads are signed by an Authenticode
> certificate chaining to a trusted root authority in the Windows trust store.
> Self-signed certificates will fail `WinVerifyTrust` on user installations,
> causing the updater to reject and delete downloaded updates, which completely
> breaks automatic updates.

Release builds must either remain unsigned (relying on published SHA-256
checksums from `update-check.txt`), or be signed with a valid, publicly trusted
Authenticode certificate through one of the supported methods below.

## Signing options

### 1. Azure Trusted Signing (Microsoft Artifact Signing)

Azure Trusted Signing is a managed Microsoft cloud signing service that provides
publicly trusted Authenticode certificates without hardware tokens.

The build workflow in `.github/workflows/build.yml` automatically signs release
executables using `azure/trusted-signing-action` when the required Azure credentials
are configured in GitHub:

- **Repository Secrets:**
  - `AZURE_TENANT_ID`: Azure tenant ID for the service principal.
  - `AZURE_CLIENT_ID`: App registration client ID.
  - `AZURE_CLIENT_SECRET`: App registration client secret (or configure federated OIDC credentials).
- **Repository Variables (or Secrets):**
  - `AZURE_TRUSTED_SIGNING_ACCOUNT`: Trusted Signing Account name.
  - `AZURE_TRUSTED_SIGNING_PROFILE`: Certificate profile name within the account.
  - `AZURE_TRUSTED_SIGNING_ENDPOINT` (optional): Regional service endpoint (defaults to `https://eus.codesigning.azure.net/`).

The Service Principal must have the `Trusted Signing Certificate Profile Signer`
role assigned on the signing account or certificate profile.

### 2. SignPath Foundation (Free OSS Signing)

The [SignPath Foundation](https://signpath.org) provides free Authenticode code
signing for qualifying open-source projects via [SignPath.io](https://signpath.io).

#### Owner setup steps:

1. **Verify eligibility:**
   - Public repository licensed under an OSI-approved license (GPL-3.0 in `COPYING`).
   - Project has an existing release (e.g. `v0.5.0`) and is actively maintained.
   - All repository maintainers have Multi-Factor Authentication (MFA) enabled on GitHub.
   - Maintain a published code-signing policy (`docs/code-signing-policy.md`).

2. **Apply to SignPath Foundation:**
   - Fill out the application at <https://signpath.org/apply>.
   - Provide the project name, repository URL, release page, and code signing policy link.
   - Copy details from `docs/signpath-application.md`. Approval typically takes a few business days.

3. **Install the SignPath GitHub App:**
   - In the SignPath dashboard, navigate to **Trusted Build Systems**.
   - Add **GitHub.com** and install the SignPath GitHub App on `TheFunzzies/swiftleaf-pdf`.

4. **Configure Project & Policies in SignPath:**
   - Create a project named `swiftleaf-pdf` tied to the GitHub repository.
   - Configure signing policies (e.g., `release-signing` for tags matching `refs/tags/v*` on `main`).
   - Create a CI user with the "Submitter" role and generate an API token.

5. **Configure GitHub Repository Secrets:**
   - In repository **Settings > Secrets and variables > Actions**:
     - `SIGNPATH_API_TOKEN`: The API token from SignPath.
     - `SIGNPATH_ORGANIZATION_ID`: The SignPath organization ID (from the dashboard).

6. **Integrate into Release Workflow:**
   - Use `signpath/github-action-submit-signing-request` to submit the unsigned
     executables artifact to SignPath for signing, then publish the signed output.

### 3. PFX Certificate (Legacy / Custom)

If you hold a standard Authenticode code-signing certificate as a PFX file:

1. Base64-encode the PFX without line breaks:
   ```powershell
   [Convert]::ToBase64String([IO.File]::ReadAllBytes('signing.pfx'))
   ```
2. In GitHub repository **Settings > Secrets and variables > Actions**, add:
   - `SIGN_CERT_PFX_BASE64`: The base64-encoded PFX content.
   - `SIGN_CERT_PASSWORD`: The password for the certificate.

The workflow will extract the certificate into a temporary path on the runner, sign
both `Swiftleaf.exe` and `Swiftleaf-install.exe` using `signtool.exe` with SHA-256 and
DigiCert RFC 3161 timestamping, and securely delete the temporary PFX file.
