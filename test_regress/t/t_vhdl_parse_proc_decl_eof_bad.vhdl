-- DESCRIPTION: Verilator: a file ending inside a process declarative part is a parse error
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- The process never reaches its begin.

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
  signal a : bit;
begin
  p : process (a)
    variable v : bit;
