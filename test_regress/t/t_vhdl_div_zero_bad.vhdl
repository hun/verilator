-- DESCRIPTION: Verilator: VHDL division by zero is refused
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- Dividing by zero is an error (IEEE 1076-2008 5.2.3.1; subset row 4.5)

entity t is
  port (
    y : out integer
  );
end;

architecture rtl of t is
  constant q : integer := 100 / 0;
begin
  y <= q;
end;
