import 'package:cloud_firestore/cloud_firestore.dart';
import 'package:firebase_auth/firebase_auth.dart';
import 'package:firebase_core/firebase_core.dart';

import '../models/app_user.dart';

/// Wraps Firebase Auth (email/password) and the matching `users/{uid}` Firestore
/// profile doc that carries the app role (admin | user | device).
class AuthService {
  final FirebaseAuth _auth = FirebaseAuth.instance;
  final FirebaseFirestore _db = FirebaseFirestore.instance;

  Stream<User?> get authStateChanges => _auth.authStateChanges();

  User? get currentUser => _auth.currentUser;

  CollectionReference<Map<String, dynamic>> get _usersRef =>
      _db.collection('users');

  Future<UserCredential> signIn({
    required String email,
    required String password,
  }) {
    return _auth.signInWithEmailAndPassword(email: email, password: password);
  }

  Future<void> signOut() => _auth.signOut();

  Stream<AppUser?> watchAppUser(String uid) {
    return _usersRef.doc(uid).snapshots().map((doc) {
      if (!doc.exists) return null;
      return AppUser.fromFirestore(doc);
    });
  }

  Stream<List<AppUser>> watchAllUsers() {
    return _usersRef.orderBy('createdAt', descending: true).snapshots().map(
          (snap) => snap.docs.map(AppUser.fromFirestore).toList(),
        );
  }

  /// Admin-only: creates a new Firebase Auth account + Firestore profile
  /// without disturbing the admin's own signed-in session. Uses a throwaway
  /// secondary [FirebaseApp] instance for the account creation call, then
  /// writes the profile doc using the *primary* app (still signed in as the
  /// admin), which is what Firestore security rules check against.
  Future<void> createUserAsAdmin({
    required String name,
    required String email,
    required String password,
    required String role,
  }) async {
    final secondaryApp = await Firebase.initializeApp(
      name: 'secondaryApp_${DateTime.now().microsecondsSinceEpoch}',
      options: Firebase.app().options,
    );
    try {
      final secondaryAuth = FirebaseAuth.instanceFor(app: secondaryApp);
      final credential = await secondaryAuth.createUserWithEmailAndPassword(
        email: email,
        password: password,
      );
      final uid = credential.user!.uid;
      await secondaryAuth.signOut();

      final user = AppUser(
        uid: uid,
        name: name,
        email: email,
        role: role,
        createdAt: DateTime.now(),
      );
      await _usersRef.doc(uid).set(user.toFirestore());
    } finally {
      await secondaryApp.delete();
    }
  }

  /// Admin-only: updates another user's display name and/or role.
  /// (Email changes are out of scope — they require the Admin SDK.)
  Future<void> updateUserAsAdmin({
    required AppUser existing,
    required String name,
    required String role,
  }) async {
    final updated = AppUser(
      uid: existing.uid,
      name: name,
      email: existing.email,
      role: role,
      createdAt: existing.createdAt,
    );
    await _usersRef.doc(existing.uid).set(updated.toFirestore());
  }

  /// Self-service: updates the signed-in user's own display name (role unchanged).
  Future<void> updateOwnName({required AppUser existing, required String name}) async {
    final updated = AppUser(
      uid: existing.uid,
      name: name,
      email: existing.email,
      role: existing.role,
      createdAt: existing.createdAt,
    );
    await _usersRef.doc(existing.uid).set(updated.toFirestore());
  }
}
