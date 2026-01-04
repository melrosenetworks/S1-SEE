# Contributing to S1-SEE

Thank you for your interest in contributing to S1-SEE! This document provides guidelines and instructions for contributing.

## Getting Started

1. **Fork the repository** on GitHub from [https://github.com/melrosenetworks/S1-SEE](https://github.com/melrosenetworks/S1-SEE)
2. **Clone your fork** locally:
   ```bash
   git clone https://github.com/your-username/S1-SEE.git
   cd S1-SEE
   ```
3. **Create a branch** for your changes:
   ```bash
   git checkout -b feature/your-feature-name
   ```

## Development Setup

Follow the build instructions in [BUILD.md](BUILD.md) to set up your development environment.

## Code Style

- **C++20**: Use modern C++ features where appropriate
- **Formatting**: Follow the existing code style in the repository
- **Headers**: All source files should include the standard header format:
  ```cpp
  /*
   * Melrose Networks (Melrose Labs Ltd) - https://melrosenetworks.com
   * Date: YYYY-MM-DD
   * Support: support@melrosenetworks.com
   * Disclaimer: Provided "as is" without warranty; use at your own risk.
   * Title: filename.cpp
   * Description: Brief description of the file's purpose
   */
  ```
- **Naming**: Use descriptive names, follow existing conventions
- **Comments**: Add comments for complex logic, but avoid obvious comments

## Making Changes

1. **Write tests** for new functionality
2. **Update documentation** (README.md, code comments) as needed
3. **Ensure all tests pass**:
   ```bash
   cd build
   ./test_ue_context
   ./test_correlator
   ./test_integration
   ```
4. **Check for linter errors** and fix any issues

## Submitting Changes

1. **Commit your changes** with clear, descriptive commit messages:
   ```bash
   git commit -m "Add feature: brief description"
   ```
2. **Push to your fork**:
   ```bash
   git push origin feature/your-feature-name
   ```
3. **Create a Pull Request** on GitHub with:
   - Clear title and description
   - Reference to any related issues
   - Summary of changes
   - Testing performed

## Pull Request Guidelines

- Keep PRs focused on a single feature or fix
- Include tests for new functionality
- Update documentation as needed
- Ensure CI checks pass (if applicable)
- Respond to review feedback promptly

## Areas for Contribution

- **Bug fixes**: Fix issues reported in the issue tracker
- **Features**: Implement features from the roadmap
- **Documentation**: Improve documentation and examples
- **Tests**: Add test coverage
- **Performance**: Optimize existing code
- **Adapters**: Implement Kafka/AMQP/NATS adapters (currently skeletons)

## Questions?

If you have questions, please:
- Open an issue for discussion
- Contact support@melrosenetworks.com

Thank you for contributing to S1-SEE!

