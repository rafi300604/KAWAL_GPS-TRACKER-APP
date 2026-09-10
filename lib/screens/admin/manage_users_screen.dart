import 'package:flutter/material.dart';

import '../../models/app_user.dart';
import '../../services/auth_service.dart';
import 'create_user_dialog.dart';
import 'edit_user_dialog.dart';

/// Simplified admin user management (list/search/filter, create, edit
/// name/role). Unlike the reference app's AdminUsersPage.vue, there is no
/// device-to-vessel assignment screen — the single device's identity is
/// fixed in Firestore/firmware config, not managed through this UI.
class ManageUsersScreen extends StatefulWidget {
  const ManageUsersScreen({super.key});

  @override
  State<ManageUsersScreen> createState() => _ManageUsersScreenState();
}

class _ManageUsersScreenState extends State<ManageUsersScreen> {
  final AuthService _authService = AuthService();
  String _search = '';
  String _roleFilter = '';
  bool _busy = false;

  Future<void> _createUser() async {
    final draft = await showDialog<CreateUserDraft>(
      context: context,
      builder: (_) => const CreateUserDialog(),
    );
    if (draft == null) return;
    setState(() => _busy = true);
    try {
      await _authService.createUserAsAdmin(
        name: draft.name,
        email: draft.email,
        password: draft.password,
        role: draft.role,
      );
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context)
            .showSnackBar(SnackBar(content: Text('Gagal membuat pengguna: $e')));
      }
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  Future<void> _editUser(AppUser user) async {
    final result = await showDialog<EditUserResult>(
      context: context,
      builder: (_) => EditUserDialog(user: user),
    );
    if (result == null) return;
    setState(() => _busy = true);
    try {
      await _authService.updateUserAsAdmin(
        existing: user,
        name: result.name,
        role: result.role,
      );
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context)
            .showSnackBar(SnackBar(content: Text('Gagal memperbarui: $e')));
      }
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  Color _roleColor(BuildContext context, String role) {
    switch (role) {
      case 'admin':
        return Colors.blue;
      case 'device':
        return Colors.purple;
      default:
        return Colors.green;
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Kelola Pengguna'),
        actions: [
          IconButton(
            icon: const Icon(Icons.person_add_alt_outlined),
            onPressed: _busy ? null : _createUser,
          ),
        ],
      ),
      body: Column(
        children: [
          Padding(
            padding: const EdgeInsets.all(12),
            child: Row(
              children: [
                Expanded(
                  child: TextField(
                    decoration: const InputDecoration(
                      prefixIcon: Icon(Icons.search),
                      hintText: 'Cari nama atau email',
                      isDense: true,
                    ),
                    onChanged: (v) => setState(() => _search = v.toLowerCase()),
                  ),
                ),
                const SizedBox(width: 12),
                DropdownButton<String>(
                  value: _roleFilter,
                  items: const [
                    DropdownMenuItem(value: '', child: Text('Semua Role')),
                    DropdownMenuItem(value: 'admin', child: Text('Admin')),
                    DropdownMenuItem(value: 'user', child: Text('User')),
                    DropdownMenuItem(value: 'device', child: Text('Device')),
                  ],
                  onChanged: (v) => setState(() => _roleFilter = v ?? ''),
                ),
              ],
            ),
          ),
          if (_busy) const LinearProgressIndicator(),
          Expanded(
            child: StreamBuilder<List<AppUser>>(
              stream: _authService.watchAllUsers(),
              builder: (context, snapshot) {
                if (!snapshot.hasData) {
                  return const Center(child: CircularProgressIndicator());
                }
                var users = snapshot.data!;
                if (_search.isNotEmpty) {
                  users = users
                      .where((u) =>
                          u.name.toLowerCase().contains(_search) ||
                          u.email.toLowerCase().contains(_search))
                      .toList();
                }
                if (_roleFilter.isNotEmpty) {
                  users = users.where((u) => u.role == _roleFilter).toList();
                }
                if (users.isEmpty) {
                  return const Center(child: Text('Tidak ada pengguna'));
                }
                return ListView.builder(
                  itemCount: users.length,
                  itemBuilder: (context, index) {
                    final u = users[index];
                    return ListTile(
                      leading: CircleAvatar(
                        backgroundColor: _roleColor(context, u.role).withValues(alpha: 0.15),
                        child: Text(
                          u.name.isNotEmpty ? u.name[0].toUpperCase() : '?',
                          style: TextStyle(color: _roleColor(context, u.role)),
                        ),
                      ),
                      title: Text(u.name),
                      subtitle: Text(u.email),
                      trailing: Row(
                        mainAxisSize: MainAxisSize.min,
                        children: [
                          Chip(
                            label: Text(u.role),
                            backgroundColor: _roleColor(context, u.role).withValues(alpha: 0.15),
                            labelStyle: TextStyle(color: _roleColor(context, u.role)),
                            visualDensity: VisualDensity.compact,
                          ),
                          IconButton(
                            icon: const Icon(Icons.edit_outlined),
                            onPressed: _busy ? null : () => _editUser(u),
                          ),
                        ],
                      ),
                    );
                  },
                );
              },
            ),
          ),
        ],
      ),
    );
  }
}
