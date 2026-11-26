-- DESCRIPTION: Verilator: VHDL file declaration with no semicolon before the end of the file
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- The file declaration below is not terminated by a semicolon before the end of the file.
-- It is skipped as a declaration without synthesis meaning, and that scan reaches the end.

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
  file f : text
